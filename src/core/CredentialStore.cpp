// libsecret debe ir antes que cualquier encabezado de Qt: glib define
// «signals», «slots» y «emit» como macros y QtREDP reusa esos nombres.
#ifdef QTRDP_HAVE_LIBSECRET
#include <libsecret/secret.h>
#endif

#include "core/CredentialStore.h"

#include <QDir>
#include <QFile>
#include <QMap>
#include <QStandardPaths>
#include <QTextStream>

namespace qtrdp {

namespace {

const char kSchemaName[] = "org.qtrdp.Session";

#ifdef QTRDP_HAVE_LIBSECRET

class SecretCredentialStore : public CredentialStore
{
public:
    ~SecretCredentialStore() override
    {
        if (m_schema)
            secret_schema_unref(m_schema);
    }

    bool isAvailable() const override
    {
        if (!ensureSchema())
            return false;

        // La creación del esquema siempre funciona; lo que puede faltar es el
        // servicio de secretos (gnome-keyring, KWallet...) en el escritorio.
        GError* gerror = nullptr;
        SecretService* service = secret_service_get_sync(SECRET_SERVICE_NONE, nullptr, &gerror);
        if (gerror)
        {
            g_error_free(gerror);
            return false;
        }

        g_object_unref(service);
        return true;
    }

    QString backendName() const override
    {
        return QStringLiteral("almacén de secretos del sistema");
    }

    QString read(const QString& credentialId, QString* error) const override
    {
        auto* self = const_cast<SecretCredentialStore*>(this);
        if (!self->ensureSchema())
        {
            setError(error, QStringLiteral("El almacén de secretos no está disponible."));
            return {};
        }

        GError* gerror = nullptr;
        GHashTable* attributes = buildAttributes(credentialId);
        gchar* secret = secret_password_lookupv_sync(self->m_schema, attributes, nullptr,
                                                    &gerror);
        g_hash_table_unref(attributes);

        if (gerror)
        {
            setError(error, QString::fromUtf8(gerror->message));
            g_error_free(gerror);
            return {};
        }

        if (!secret)
            return {};

        const QString value = QString::fromUtf8(secret);
        secret_password_free(secret);
        return value;
    }

    bool write(const QString& credentialId, const QString& secret,
               QString* error) override
    {
        if (!ensureSchema())
        {
            setError(error, QStringLiteral("El almacén de secretos no está disponible."));
            return false;
        }

        GError* gerror = nullptr;
        GHashTable* attributes = buildAttributes(credentialId);
        const QByteArray password = secret.toUtf8();
        const bool ok =
	    secret_password_storev_sync(m_schema, attributes, SECRET_COLLECTION_DEFAULT,
					 "QtRDP", password.constData(), nullptr, &gerror) == TRUE;
        g_hash_table_unref(attributes);

        if (gerror)
        {
            setError(error, QString::fromUtf8(gerror->message));
            g_error_free(gerror);
            return false;
        }

        return ok;
    }

    bool remove(const QString& credentialId, QString* error) override
    {
        if (!ensureSchema())
        {
            setError(error, QStringLiteral("El almacén de secretos no está disponible."));
            return false;
        }

        GError* gerror = nullptr;
        GHashTable* attributes = buildAttributes(credentialId);
        secret_password_clearv_sync(m_schema, attributes, nullptr, &gerror);
        g_hash_table_unref(attributes);

        if (gerror)
        {
            setError(error, QString::fromUtf8(gerror->message));
            g_error_free(gerror);
            return false;
        }

        return true;
    }

private:
    static void setError(QString* error, const QString& message)
    {
        if (error)
            *error = message;
    }

    static void addAttribute(GHashTable* attributes, const char* key, const char* value)
    {
        g_hash_table_insert(attributes, g_strdup(key), g_strdup(value));
    }

    static GHashTable* buildAttributes(const QString& credentialId)
    {
        GHashTable* attributes =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        addAttribute(attributes, "application", "qtrdp");

        const QStringList parts = credentialId.split(QLatin1Char('|'));
        if (parts.size() >= 1)
            addAttribute(attributes, "server", parts.at(0).toUtf8().constData());
        if (parts.size() >= 2)
            addAttribute(attributes, "port", parts.at(1).toUtf8().constData());
        if (parts.size() >= 3)
            addAttribute(attributes, "user", parts.at(2).toUtf8().constData());
        if (parts.size() >= 4)
            addAttribute(attributes, "domain", parts.at(3).toUtf8().constData());

        return attributes;
    }

    bool ensureSchema() const
    {
        if (m_schema)
            return true;

        auto* self = const_cast<SecretCredentialStore*>(this);
        self->m_schema = secret_schema_new(
            kSchemaName, SECRET_SCHEMA_NONE,
            "application", SECRET_SCHEMA_ATTRIBUTE_STRING,
            "server", SECRET_SCHEMA_ATTRIBUTE_STRING,
            "port", SECRET_SCHEMA_ATTRIBUTE_STRING,
            "user", SECRET_SCHEMA_ATTRIBUTE_STRING,
            "domain", SECRET_SCHEMA_ATTRIBUTE_STRING,
            nullptr);
        return self->m_schema != nullptr;
    }

    mutable SecretSchema* m_schema = nullptr;
};

#endif // QTRDP_HAVE_LIBSECRET

// Respaldo en texto plano del archivo de configuración, solo para equipos sin
// almacén de secretos. QtRDP avisa al usuario cuando lo usa.
class FileCredentialStore : public CredentialStore
{
public:
    bool isAvailable() const override
    {
        return true;
    }

    QString filePath() const override
    {
        return QDir(configDirectory()).filePath(QStringLiteral("credenciales.dat"));
    }

    QString backendName() const override
    {
        return QStringLiteral("archivo de credenciales sin cifrar");
    }

    QString read(const QString& credentialId, QString* error) const override
    {
        const QMap<QString, QString> entries = loadEntries(error);
        const QString encoded = entries.value(credentialId);
        if (encoded.isEmpty())
            return {};

        const QByteArray decoded = QByteArray::fromBase64(encoded.toLatin1());
        if (decoded.isEmpty())
        {
            if (error)
                *error = QStringLiteral("La credencial guardada está dañada.");
            return {};
        }

        return QString::fromUtf8(decoded);
    }

    bool write(const QString& credentialId, const QString& secret, QString* error) override
    {
        QMap<QString, QString> entries = loadEntries(error);
        entries.insert(credentialId,
		       QString::fromLatin1(secret.toUtf8().toBase64()));
        return saveEntries(entries, error);
    }

    bool remove(const QString& credentialId, QString* error) override
    {
        QMap<QString, QString> entries = loadEntries(error);
        if (entries.remove(credentialId) == 0)
            return true;
        return saveEntries(entries, error);
    }

    static QString configDirectory()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }

    static bool ensureConfigDirectory()
    {
        QDir directory(configDirectory());
        if (directory.exists())
            return true;
        return QDir().mkpath(directory.absolutePath());
    }

private:
    QMap<QString, QString> loadEntries(QString* error) const
    {
        QMap<QString, QString> entries;
        QFile file(filePath());
        if (!file.exists())
            return entries;

        if (!file.open(QIODevice::ReadOnly))
        {
            if (error)
                *error = QStringLiteral("No se pudo leer el archivo de credenciales.");
            return entries;
        }

        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        while (!stream.atEnd())
        {
            const QString line = stream.readLine();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const int separator = line.indexOf(QLatin1Char('='));
            if (separator <= 0)
                continue;
            entries.insert(line.left(separator), line.mid(separator + 1));
        }

        file.close();
        return entries;
    }

    bool saveEntries(const QMap<QString, QString>& entries, QString* error)
    {
        if (!ensureConfigDirectory())
        {
            if (error)
                *error = QStringLiteral("No se pudo crear la carpeta de configuración.");
            return false;
        }

        QFile file(filePath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            if (error)
                *error = QStringLiteral("No se pudo escribir el archivo de credenciales.");
            return false;
        }

        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << QStringLiteral("# QtRDP: credenciales en texto plano\n");
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
            stream << it.key() << QLatin1Char('=') << it.value() << QLatin1Char('\n');
        stream.flush();
        file.close();

        // Solo el usuario actual debe poder leer el archivo.
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        return true;
    }
};

CredentialStore*& instanceRef()
{
    static CredentialStore* store = nullptr;
    return store;
}

} // namespace

CredentialStore* CredentialStore::instance()
{
    if (!instanceRef())
    {
#ifdef QTRDP_HAVE_LIBSECRET
        auto* secret = new SecretCredentialStore;
        if (secret->isAvailable())
            instanceRef() = secret;
        else
            delete secret;
#endif
        if (!instanceRef())
            instanceRef() = new FileCredentialStore;
    }

    return instanceRef();
}

CredentialStore* CredentialStore::createFileStore()
{
    return new FileCredentialStore;
}

void CredentialStore::setInstance(CredentialStore* store)
{
    if (instanceRef() != store)
    {
        delete instanceRef();
        instanceRef() = store;
    }
}

QString CredentialStore::credentialId(const ConnectionProfile& profile)
{
    return QStringLiteral("%1|%2|%3|%4")
	.arg(profile.host.toLower(), QString::number(profile.port), profile.username.toLower(),
	     profile.domain.toLower());
}

} // namespace qtrdp
