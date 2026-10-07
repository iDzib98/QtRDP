#include "core/XFreeRdpLauncher.h"

#include <QDir>
#include <QCoreApplication>
#include <QHostAddress>
#include <QHostInfo>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusArgument>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace qtrdp {

namespace {

// Resuelve el nombre del equipo con la pila DNS del sistema y, si falla,
// consulta Avahi por D-Bus para nombres mDNS (.local). También prueba el
// sufijo .local cuando el nombre corto no se resuelve (DZ-RDP → dz-rdp.local).
QString resolveHost(const QString& host)
{
    bool isIp = false;
    QHostAddress address(host);
    if (!address.isNull())
        isIp = true;

    if (isIp)
        return host;

    // 1. Resolución DNS/mDNS estándar.
    const QHostInfo info = QHostInfo::fromName(host);
    if (info.error() == QHostInfo::NoError && !info.addresses().isEmpty())
    {
        for (const QHostAddress& addr : info.addresses())
        {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol)
                return addr.toString();
        }
        return info.addresses().first().toString();
    }

    // 2. Consulta directa a Avahi por D-Bus (funciona en Flatpak donde
    //    nss-mdns no está disponible).
    const auto avahiResolve = [](const QString& name) -> QString
    {
        QDBusConnection bus = QDBusConnection::systemBus();
        if (!bus.isConnected())
        {
            return {};
        }

        QDBusInterface avahi(QStringLiteral("org.freedesktop.Avahi"),
                             QStringLiteral("/"),
                             QStringLiteral("org.freedesktop.Avahi.Server"),
                             bus);
        if (!avahi.isValid())
        {
            return {};
        }

        // ResolveHostName(interface, protocol, name, aprotocol, flags)
        // Retorna (interface, protocol, name, aprotocol, address, flags).
        QDBusMessage msg = avahi.call(QStringLiteral("ResolveHostName"),
                                       -1, -1, name, -1, 0u);
        if (msg.type() != QDBusMessage::ReplyMessage)
        {
            return {};
        }

        // El retorno es un tuple; QDBusMessage lo entrega como un argumento.
        const QVariantList args = msg.arguments();
        if (args.isEmpty())
            return {};

        // El retorno son 6 argumentos: (i,i,s,i,s,u). La dirección es el 5º.
        if (args.size() < 6)
        {
            return {};
        }
        const QString address = args.at(4).toString();
        return address;
    };

    QString resolved = avahiResolve(host);
    if (!resolved.isEmpty())
        return resolved;

    // 3. Fallback: si el nombre no tiene punto, probar con sufijo .local.
    if (!host.contains(QLatin1Char('.')))
    {
        const QString localName = host.toLower() + QStringLiteral(".local");
        resolved = avahiResolve(localName);
        if (!resolved.isEmpty())
            return resolved;

        // 4. Último intento con QHostInfo usando el nombre .local.
        const QHostInfo localInfo = QHostInfo::fromName(localName);
        if (localInfo.error() == QHostInfo::NoError && !localInfo.addresses().isEmpty())
        {
            for (const QHostAddress& addr : localInfo.addresses())
            {
                if (addr.protocol() == QAbstractSocket::IPv4Protocol)
                    return addr.toString();
            }
            return localInfo.addresses().first().toString();
        }
    }

    return host;
}

QString targetValue(const ConnectionProfile& profile)
{
    const QString host = resolveHost(profile.host);
    if (profile.port == 3389)
        return host;

    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('[')))
        return QStringLiteral("[%1]:%2").arg(host).arg(profile.port);
    return QStringLiteral("%1:%2").arg(host).arg(profile.port);
}

void appendDrive(QStringList& result, const QString& entry)
{
    const QString value = entry.trimmed();
    if (value.isEmpty())
        return;

    const int comma = value.indexOf(QLatin1Char(','));
    if (comma > 0)
    {
        const QString name = value.left(comma).trimmed();
        const QString path = value.mid(comma + 1).trimmed();
        if (!name.isEmpty() && !path.isEmpty())
            result.append(QStringLiteral("/drive:%1,%2").arg(name, path));
        return;
    }

    result.append(QStringLiteral("/drive:%1,%2")
                      .arg(QFileInfo(value).fileName().isEmpty()
                               ? QStringLiteral("drive")
                               : QFileInfo(value).fileName(),
                           value));
}

} // namespace

XFreeRdpLauncher::ConnectionError XFreeRdpLauncher::analyzeOutput(const QString& output,
                                                                    int exitCode)
{
    ConnectionError result;

    // Desconexiones normales: no son errores.
    if (output.contains(QStringLiteral("ERRINFO_LOGOFF_BY_USER")) ||
        output.contains(QStringLiteral("ERRCONNECT_CONNECT_CANCELLED")))
    {
        result.isFatal = false;
        return result;
    }

    // Contraseña o credenciales incorrectas.
    if (output.contains(QStringLiteral("ERRCONNECT_LOGON_FAILURE")) ||
        output.contains(QStringLiteral("0x00020014")))
    {
        result.title = QStringLiteral("Contraseña incorrecta");
        result.friendlyMessage = QStringLiteral(
            "El usuario o la contraseña no son correctos.\n\n"
            "Verifique las credenciales e inténtelo de nuevo.");
        return result;
    }

    // Nombre de equipo no encontrado.
    if (output.contains(QStringLiteral("ERRCONNECT_DNS_NAME_NOT_FOUND")) ||
        output.contains(QStringLiteral("ERRCONNECT_DNS_NAME_NOT_FOUND")) ||
        output.contains(QStringLiteral("Nombre o servicio desconocido")))
    {
        result.title = QStringLiteral("Equipo no encontrado");
        result.friendlyMessage = QStringLiteral(
            "No se pudo resolver el nombre del equipo.\n\n"
            "Verifique que el nombre sea correcto o use la dirección IP.");
        return result;
    }

    // No se pudo conectar (equipo apagado, firewall, puerto cerrado).
    if (output.contains(QStringLiteral("ERRCONNECT_CONNECT_FAILED")) ||
        output.contains(QStringLiteral("ERRCONNECT_CONNECT_TRANSPORT_FAILED")) ||
        output.contains(QStringLiteral("Connection refused")) ||
        output.contains(QStringLiteral("Connection timed out")))
    {
        result.title = QStringLiteral("No se pudo conectar");
        result.friendlyMessage = QStringLiteral(
            "No se estableció la conexión con el equipo remoto.\n\n"
            "Compruebe que el equipo esté encendido, que el servicio de "
            "Escritorio remoto esté activo y que no haya un firewall bloqueando.");
        return result;
    }

    // Error de certificado.
    if (output.contains(QStringLiteral("ERRCONNECT_CERTIFICATE")) ||
        output.contains(QStringLiteral("CERTIFICATE VERIFICATION FAILED")))
    {
        result.title = QStringLiteral("Error de certificado");
        result.friendlyMessage = QStringLiteral(
            "El certificado del equipo remoto no es válido.\n\n"
            "Si confía en este equipo, puede conectar de todas formas.");
        return result;
    }

    // Seguridad / NLA.
    if (output.contains(QStringLiteral("ERRCONNECT_SECURITY_NEGO")) ||
        output.contains(QStringLiteral("ERRCONNECT_HYBRID_REQUIRED")))
    {
        result.title = QStringLiteral("Error de autenticación");
        result.friendlyMessage = QStringLiteral(
            "El equipo rechazó la autenticación.\n\n"
            "Verifique que las credenciales sean correctas y que el "
            "protocolo de seguridad sea compatible.");
        return result;
    }

    // Kerberos / KDC.
    if (output.contains(QStringLiteral("Cannot find KDC")) ||
        output.contains(QStringLiteral("ERRCONNECT_KDC_UNREACHABLE")))
    {
        result.title = QStringLiteral("Dominio no disponible");
        result.friendlyMessage = QStringLiteral(
            "No se encontró el controlador de dominio.\n\n"
            "Si no usa un dominio, deje el campo «Dominio» vacío.");
        return result;
    }

    // Caída / cierre inesperado (exit code 134 = SIGABRT).
    if (exitCode == 134 || output.contains(QStringLiteral("SIGABRT")) ||
        output.contains(QStringLiteral("Aborted")))
    {
        result.title = QStringLiteral("Error interno");
        result.friendlyMessage = QStringLiteral(
            "xfreerdp se cerró inesperadamente.\n\n"
            "Esto puede deberse a un error interno o a un problema con "
            "el equipo remoto. Intente reconectar.");
        return result;
    }

    // Error genérico.
    if (exitCode != 0)
    {
        result.title = QStringLiteral("Error de conexión");
        result.friendlyMessage = QStringLiteral(
            "La conexión terminó con un error.\n\n"
            "Consulte los detalles técnicos para más información.");
        return result;
    }

    result.isFatal = false;
    return result;
}

XFreeRdpLauncher::XFreeRdpLauncher(QObject* parent) : QObject(parent), m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error)
    {
        if (error == QProcess::FailedToStart)
            Q_EMIT this->error(QStringLiteral("No se pudo ejecutar xfreerdp. "
                                             "Comprueba que está instalado en el sistema."));
    });

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]()
    {
        while (m_process->canReadLine())
        {
            const QString line = QString::fromUtf8(m_process->readLine()).trimmed();
            if (!line.isEmpty())
            {
                m_outputLines.append(line);
                if (line.contains(QStringLiteral("Logon Info")) ||
                    line.contains(QStringLiteral("Sesión activa")))
                    m_connected = true;
                Q_EMIT outputReceived(line);
            }
        }
    });

    connect(m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            [this](int code, QProcess::ExitStatus status)
            {
                const QString remaining = QString::fromUtf8(m_process->readAllStandardOutput()).trimmed();
                for (const QString& line : remaining.split(QLatin1Char('\n')))
                {
                    const QString trimmed = line.trimmed();
                    if (!trimmed.isEmpty())
                    {
                        m_outputLines.append(trimmed);
                        Q_EMIT outputReceived(trimmed);
                    }
                }

                const QString output = m_outputLines.join(QLatin1Char('\n'));
                const ConnectionError err = analyzeOutput(output, code);

                if (err.isFatal)
                {
                    ConnectionError detailed = err;
                    detailed.technicalDetails = output.isEmpty()
                        ? QStringLiteral("xfreerdp terminó con código %1 (sin salida)")
                              .arg(code)
                        : QStringLiteral("Código de salida: %1\n\n%2")
                              .arg(code).arg(output.right(3000));
                    Q_EMIT connectionError(detailed);
                }
                Q_EMIT finished(code, status == QProcess::NormalExit);
            });
}

XFreeRdpLauncher::~XFreeRdpLauncher()
{
    stop();
}

QString XFreeRdpLauncher::programPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& name : { QStringLiteral("xfreerdp3"), QStringLiteral("xfreerdp") })
    {
        const QString bundled = QDir(appDir).filePath(name);
        if (QFileInfo::exists(bundled) && QFileInfo(bundled).isExecutable())
            return bundled;

        const QString appImageBin = QDir(appDir).filePath(QStringLiteral("../bin/") + name);
        if (QFileInfo::exists(appImageBin) && QFileInfo(appImageBin).isExecutable())
            return QFileInfo(appImageBin).canonicalFilePath();

        const QString system = QStandardPaths::findExecutable(name);
        if (!system.isEmpty())
            return system;
    }
    return QStringLiteral("xfreerdp3");
}

QStringList XFreeRdpLauncher::arguments(const ConnectionProfile& profile,
                                        const SessionBackend::Credentials& credentials)
{
    QStringList result;
    result << QStringLiteral("/v:%1").arg(targetValue(profile));
    result << QStringLiteral("/cert:tofu");
    const QString title = !profile.name.isEmpty() ? profile.name : profile.host;
    result << QStringLiteral("/title:%1").arg(title);

    // Si el usuario ya incluye DOMINIO\usuario, no pasar /d: por separado
    // para evitar que NLA busque un KDC para un dominio residual.
    QString user = credentials.username;
    QString domain = credentials.domain;
    const int backslash = user.indexOf(QLatin1Char('\\'));
    if (backslash >= 0)
    {
        if (domain.isEmpty())
            domain = user.left(backslash);
        user = user.mid(backslash + 1);
    }

    // WORKGROUP es el grupo de trabajo por defecto de Windows; NLA lo trata
    // como «sin dominio» y no debe enviarse como realm Kerberos.
    if (domain.compare(QLatin1String("WORKGROUP"), Qt::CaseInsensitive) == 0)
        domain.clear();

    if (!user.isEmpty())
        result << QStringLiteral("/u:%1").arg(user);
    if (!domain.isEmpty())
        result << QStringLiteral("/d:%1").arg(domain);
    if (!credentials.password.isEmpty())
        result << QStringLiteral("/p:%1").arg(credentials.password);

    if (profile.dynamicResolution)
        result << QStringLiteral("/dynamic-resolution");
    else if ((profile.desktopWidth > 0) && (profile.desktopHeight > 0))
        result << QStringLiteral("/size:%1x%2").arg(profile.desktopWidth).arg(profile.desktopHeight);

    if (profile.redirectClipboard)
        result << QStringLiteral("/clipboard");
    if (profile.redirectAudio)
        result << QStringLiteral("/sound");
    if (profile.redirectMicrophone)
        result << QStringLiteral("/microphone");
    if (profile.redirectPrinters)
        result << QStringLiteral("/printer");
    if (profile.redirectHomeDrive)
        result << QStringLiteral("/drive:home,%1").arg(QDir::homePath());
    if (profile.redirectDrives)
    {
        for (const QString& drive : profile.drives)
            appendDrive(result, drive);
    }

    if (profile.useMultimon)
        result << QStringLiteral("/multimon");
    if (profile.gatewayEnabled && !profile.gatewayHostname.isEmpty())
    {
        result << QStringLiteral("/g:%1").arg(profile.gatewayHostname);
        if (!profile.gatewayUsername.isEmpty())
            result << QStringLiteral("/gu:%1").arg(profile.gatewayUsername);
        if (profile.gatewayPort > 0)
            result << QStringLiteral("/gp:%1").arg(profile.gatewayPort);
    }
    if (!profile.remoteProgram.isEmpty())
        result << QStringLiteral("/app:%1").arg(profile.remoteProgram);
    if (profile.colorDepth > 0)
        result << QStringLiteral("/bpp:%1").arg(profile.colorDepth);

    return result;
}

bool XFreeRdpLauncher::start(const ConnectionProfile& profile,
                             const SessionBackend::Credentials& credentials)
{
    if (isRunning() || !profile.isValid())
        return false;

    m_connected = false;
    m_outputLines.clear();
    const QString program = programPath();
    Q_EMIT outputReceived(QStringLiteral("Ejecutando: %1").arg(program));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    const QString bundledPlugins =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../lib/freerdp3"));
    if (QDir(bundledPlugins).exists())
    {
        environment.insert(QStringLiteral("FREERDP_PLUGIN_PATH"), bundledPlugins);
        environment.insert(QStringLiteral("FREERDP_ADDIN_PATH"), bundledPlugins);
    }
    m_process->setProcessEnvironment(environment);
    const QStringList args = arguments(profile, credentials);
    Q_EMIT outputReceived(QStringLiteral("Argumentos: %1").arg(args.join(QLatin1Char(' '))));
    m_process->start(program, args);
    return m_process->waitForStarted(3000);
}

void XFreeRdpLauncher::stop()
{
    if (!m_process || !isRunning())
        return;
    m_process->terminate();
    if (!m_process->waitForFinished(2000))
        m_process->kill();
}

bool XFreeRdpLauncher::isRunning() const
{
    return m_process && (m_process->state() != QProcess::NotRunning);
}

} // namespace qtrdp
