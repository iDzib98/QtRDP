#include "core/ConnectionProfile.h"
#include "core/CredentialStore.h"
#include "core/GnomeFullscreen.h"
#include "core/ProfileStore.h"
#include "core/RdpFile.h"
#include "core/SessionBackend.h"
#include "core/XFreeRdpLauncher.h"
#include "ui/ConnectionDialog.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QStyleFactory>
#include <QTranslator>

#include <cstdio>
#include <memory>

namespace {

struct StartupOptions
{
    bool fullScreen = false;
    bool listMonitors = false;
    qtrdp::ConnectionProfile profile;
    bool hasProfile = false;
    QString cliPassword;
    QString rdpFile; // Archivo .rdp pasado por línea de órdenes.
};

StartupOptions parseCommandLine(const QApplication& app)
{
    StartupOptions options;

    QCommandLineParser parser;
    parser.setApplicationDescription(
	    QCoreApplication::translate("main", "Cliente de escritorio remoto para Linux."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption fullScreen(
	    { QStringLiteral("f"), QStringLiteral("full-screen") },
	    QCoreApplication::translate("main", "Inicia la sesión en pantalla completa."));
    const QCommandLineOption listMonitors(
	    QStringLiteral("list-monitor-layouts"),
	    QCoreApplication::translate("main", "Muestra los monitores disponibles."));
    const QCommandLineOption user({ QStringLiteral("u"), QStringLiteral("user") },
	                          QCoreApplication::translate("main", "Usuario con el que conectar."),
	                          QCoreApplication::translate("main", "usuario"));
    const QCommandLineOption password(
	    QStringLiteral("p"),
	    QCoreApplication::translate("main", "Contraseña (no recomendado)."),
	    QCoreApplication::translate("main", "contraseña"));
    const QCommandLineOption domain(
	    QStringLiteral("d"),
	    QCoreApplication::translate("main", "Dominio de inicio de sesión."),
	    QCoreApplication::translate("main", "dominio"));
    const QCommandLineOption port(
	    QStringLiteral("port"),
	    QCoreApplication::translate("main", "Puerto del servidor (3389 por omisión)."),
	    QCoreApplication::translate("main", "puerto"), QStringLiteral("3389"));
    const QCommandLineOption protocol(
	    QStringLiteral("protocol"),
	    QCoreApplication::translate("main", "Protocolo: rdp."),
	    QCoreApplication::translate("main", "protocolo"), QStringLiteral("rdp"));
    const QCommandLineOption width(
	    QStringLiteral("width"),
	    QCoreApplication::translate("main", "Ancho del escritorio remoto."),
	    QCoreApplication::translate("main", "píxeles"), QStringLiteral("0"));
    const QCommandLineOption height(
	    QStringLiteral("height"),
	    QCoreApplication::translate("main", "Alto del escritorio remoto."),
	    QCoreApplication::translate("main", "píxeles"), QStringLiteral("0"));

    parser.addOption(fullScreen);
    parser.addOption(listMonitors);
    parser.addOption(user);
    parser.addOption(password);
    parser.addOption(domain);
    parser.addOption(port);
    parser.addOption(protocol);
    parser.addOption(width);
    parser.addOption(height);
    parser.addPositionalArgument(
	    QStringLiteral("servidor"), QCoreApplication::translate("main", "Equipo o dirección IP."));

    parser.process(app);

    options.fullScreen = parser.isSet(fullScreen);
    options.listMonitors = parser.isSet(listMonitors);

    if (options.listMonitors)
    {
        std::fputs("La enumeración de monitores requiere FreeRDP; pendiente para F5.\n",
                   stderr);
        return options;
    }

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty())
        return options;

    const QFileInfo candidate(positional.first());
    if (candidate.exists() &&
	(candidate.suffix().compare(QLatin1String("rdp"), Qt::CaseInsensitive) == 0))
    {
        options.rdpFile = candidate.absoluteFilePath();
        options.profile = qtrdp::RdpFile::read(options.rdpFile);
        options.hasProfile = options.profile.isValid();
        if (!options.hasProfile)
        {
            std::fputs(qPrintable(QStringLiteral("El archivo .rdp no indica un equipo "
						        "válido: %1\n")
                                      .arg(options.rdpFile)),
                       stderr);
        }
        return options;
    }

    options.profile.host = positional.first();
    options.profile.port = static_cast<quint16>(parser.value(port).toUShort());
    options.profile.username = parser.value(user);
    options.profile.domain = parser.value(domain);
    options.profile.desktopWidth = parser.value(width).toInt();
    options.profile.desktopHeight = parser.value(height).toInt();
    options.profile.protocol = parser.value(protocol);
    options.cliPassword = parser.value(password);
    options.hasProfile = true;

    return options;
}

QString persistPromptedCredentials(qtrdp::ConnectionProfile& profile,
                                   const qtrdp::SessionBackend::Credentials& credentials,
                                   bool rememberPassword)
{
    profile.rememberPassword = rememberPassword;
    if (!profile.id.isEmpty())
    {
        qtrdp::ProfileStore store;
        if (!store.update(profile))
            return QStringLiteral("No se pudo actualizar el perfil.");
    }

    qtrdp::CredentialStore* store = qtrdp::CredentialStore::instance();
    const QString id = qtrdp::CredentialStore::credentialId(profile);
    if (rememberPassword)
    {
        QString error;
        if (!store->write(id, credentials.password, &error))
            return error.isEmpty() ? QStringLiteral("El almacén rechazó la contraseña.") : error;
    }
    else
    {
        QString error;
        if (!store->remove(id, &error))
            return error.isEmpty() ? QStringLiteral("No se pudo borrar la contraseña anterior.")
                                   : error;
    }
    return {};
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("qtrdp"));
    QApplication::setApplicationDisplayName(QStringLiteral("QtRDP"));
    QApplication::setApplicationVersion(QStringLiteral(QTRDP_VERSION));
    QApplication::setOrganizationName(QStringLiteral("QtRDP"));
    QApplication::setOrganizationDomain(QStringLiteral("qtrdp.local"));

    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);

    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("qtrdp"), QStringLiteral("_"),
                        QCoreApplication::applicationDirPath() + QStringLiteral("/translations")))
        app.installTranslator(&translator);

    const StartupOptions options = parseCommandLine(app);
    if (options.listMonitors)
        return 0;

    qtrdp::ConnectionProfile profile = options.profile;
    qtrdp::SessionBackend::Credentials credentials;

    if (!options.hasProfile || !options.rdpFile.isEmpty())
    {
        // El mismo diálogo hace de gestor de perfiles.
        qtrdp::ConnectionDialog dialog;
        if (!options.rdpFile.isEmpty())
        {
            qtrdp::ProfileStore store;
            if (!store.importRdpFile(options.rdpFile))
            {
                std::fputs(qPrintable(QStringLiteral("No se pudo leer el archivo %1\n")
                                          .arg(options.rdpFile)),
                           stderr);
            }
            dialog.loadProfile(profile);
        }

        if (dialog.exec() != QDialog::Accepted)
            return 0;

        profile = dialog.profile();
        credentials = dialog.credentials();
    }
    else
    {
        credentials.username = profile.username;
        credentials.domain = profile.domain;
        if (!options.cliPassword.isEmpty())
        {
            credentials.password = options.cliPassword;
        }
        else if (profile.rememberPassword)
        {
            credentials.password = qtrdp::CredentialStore::instance()->read(
                qtrdp::CredentialStore::credentialId(profile));
        }
    }

    if (!profile.isValid())
    {
        QMessageBox::warning(nullptr, QStringLiteral("Conexión de Escritorio remoto"),
                             QStringLiteral("Indica un equipo o una dirección IP válidos."));
        return 1;
    }

    // Como mstsc, una contraseña ausente se solicita justo antes de conectar,
    // no se transforma en un fallo de autenticación vacío.
    bool rememberPassword = profile.rememberPassword;
    if (credentials.password.isEmpty())
    {
        if (!qtrdp::ConnectionDialog::promptForCredentials(
                nullptr, profile, credentials, rememberPassword,
                QStringLiteral("Escriba el usuario y la contraseña para conectarse al equipo remoto.")))
            return 0;
        const QString saveError =
            persistPromptedCredentials(profile, credentials, rememberPassword);
        if (!saveError.isEmpty())
        {
            QMessageBox::warning(nullptr, QStringLiteral("Contraseña no guardada"),
                                 QStringLiteral("La conexión continuará, pero no se pudo "
                                                "recordar la contraseña: %1")
                                     .arg(saveError));
        }
    }

    // La opción -f/--full-screen de la línea de órdenes se aplica como si
    // el perfil se hubiera guardado con «Pantalla completa».
    if (options.fullScreen)
        profile.extraOptions.insert(QStringLiteral("fullScreen"), true);

    // En GNOME/Wayland el toggle de FreeRDP no es fiable: QtRDP asume el
    // control de la pantalla completa (EWMH + atajo global) y pide al launcher
    // que no use /f ni la floatbar.
    std::unique_ptr<qtrdp::GnomeFullscreen> gnomeFullscreen;
    if (qtrdp::GnomeFullscreen::shouldActivate())
    {
        profile.extraOptions.insert(QStringLiteral("externalFullscreen"), true);
        gnomeFullscreen = std::make_unique<qtrdp::GnomeFullscreen>();
        std::fprintf(stderr, "[qtrdp] GNOME/Wayland: pantalla completa gestionada por QtRDP "
                             "(atajo global + EWMH)\n");
    }
    else
    {
        std::fprintf(stderr, "[qtrdp] pantalla completa nativa de FreeRDP "
                             "(XDG_CURRENT_DESKTOP=%s, XDG_SESSION_TYPE=%s)\n",
                     qgetenv("XDG_CURRENT_DESKTOP").constData(),
                     qgetenv("XDG_SESSION_TYPE").constData());
    }

    qtrdp::XFreeRdpLauncher launcher;
    bool hasError = false;
    QObject::connect(&launcher, &qtrdp::XFreeRdpLauncher::outputReceived, &app,
                     [](const QString& line)
                     {
                         std::fprintf(stderr, "[xfreerdp] %s\n", qPrintable(line));
                     });
    QObject::connect(&launcher, &qtrdp::XFreeRdpLauncher::connectionError, &app,
                     [&hasError](const qtrdp::XFreeRdpLauncher::ConnectionError& err)
                     {
                         hasError = true;
                         QMessageBox box(QMessageBox::Warning,
                                         err.title.isEmpty()
                                             ? QStringLiteral("Error de conexión")
                                             : err.title,
                                         err.friendlyMessage,
                                         QMessageBox::Ok);
                         if (!err.technicalDetails.isEmpty())
                             box.setDetailedText(err.technicalDetails);
                         box.exec();
                     });
    QObject::connect(&launcher, &qtrdp::XFreeRdpLauncher::finished, &app,
                     [&app](int, bool)
                     {
                         app.quit();
                     });

    if (!launcher.start(profile, credentials))
    {
        QMessageBox::critical(nullptr, QStringLiteral("Conexión de Escritorio remoto"),
                              QStringLiteral("No se pudo iniciar xfreerdp3."));
        return 1;
    }

    if (gnomeFullscreen)
    {
        gnomeFullscreen->setTargetPid(launcher.processId());
        gnomeFullscreen->setStartFullscreen(
            profile.extraOptions.value(QStringLiteral("fullScreen"), false).toBool());
        QObject::connect(gnomeFullscreen.get(), &qtrdp::GnomeFullscreen::shortcutReady, &app,
                         [](const QString& trigger)
                         {
                             std::fprintf(stderr,
                                          "[qtrdp] pantalla completa con %s\n",
                                          qPrintable(trigger));
                         });
        QObject::connect(gnomeFullscreen.get(), &qtrdp::GnomeFullscreen::shortcutFailed, &app,
                         [](const QString& reason)
                         {
                             std::fprintf(stderr, "[qtrdp] atajo no disponible: %s\n",
                                          qPrintable(reason));
                         });
        gnomeFullscreen->registerShortcut();
    }

    return app.exec();
}
