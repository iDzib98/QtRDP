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

QString targetValue(const ConnectionProfile& profile, bool resolveHostName)
{
    const QString host = resolveHostName ? resolveHost(profile.host) : profile.host;
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
                                        const SessionBackend::Credentials& credentials,
                                        bool resolveHostName)
{
    QStringList result;
    result << QStringLiteral("/v:%1").arg(targetValue(profile, resolveHostName));

    // «Si se producen errores de autenticación»: 2 = no conectarse (deny),
    // 1 = avisar (tofu, valor por omisión) y 0 = conectarse igualmente (ignore).
    const int authenticationLevel =
        profile.extraOptions.value(QStringLiteral("authenticationLevel"), 1).toInt();
    QString certMode = QStringLiteral("tofu");
    if (authenticationLevel == 2)
        certMode = QStringLiteral("deny");
    else if (authenticationLevel == 0)
        certMode = QStringLiteral("ignore");
    result << QStringLiteral("/cert:%1").arg(certMode);

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

    // Protocolo de seguridad forzado (/sec:nla, /sec:tls, /sec:rdp, /sec:ext).
    const QString securityProtocol =
        profile.extraOptions.value(QStringLiteral("securityProtocol")).toString();
    if (!securityProtocol.isEmpty() && securityProtocol != QLatin1String("default"))
        result << QStringLiteral("/sec:%1").arg(securityProtocol);

    if (profile.extraOptions.value(QStringLiteral("admin"), false).toBool())
        result << QStringLiteral("+admin");
    if (profile.extraOptions.value(QStringLiteral("restrictedAdmin"), false).toBool())
        result << QStringLiteral("+restricted-admin");
    if (profile.extraOptions.value(QStringLiteral("workarea"), false).toBool())
        result << QStringLiteral("+workarea");

    if (!profile.shell.isEmpty())
        result << QStringLiteral("/shell:%1").arg(profile.shell);
    const QString shellDir =
        profile.extraOptions.value(QStringLiteral("shellWorkingDirectory")).toString();
    if (!shellDir.isEmpty())
        result << QStringLiteral("/shell-dir:%1").arg(shellDir);

    const QString clientHostname =
        profile.extraOptions.value(QStringLiteral("clientHostname")).toString();
    if (!clientHostname.isEmpty())
        result << QStringLiteral("/client-hostname:%1").arg(clientHostname);

    const int timeout = profile.extraOptions.value(QStringLiteral("timeout"), 0).toInt();
    if (timeout > 0)
        result << QStringLiteral("/timeout:%1").arg(timeout);

    const int tlsSeclevel = profile.extraOptions.value(QStringLiteral("tlsSeclevel"), 0).toInt();
    if (tlsSeclevel > 0)
        result << QStringLiteral("/tls:seclevel:%1").arg(tlsSeclevel);

    const int preventLock =
        profile.extraOptions.value(QStringLiteral("preventSessionLock"), 0).toInt();
    if (preventLock > 0)
        result << QStringLiteral("/prevent-session-lock:%1").arg(preventLock);

    // Proxy explícito: /proxy:[<proto>://][<user>:<password>@]<host>[:<port>].
    if (!profile.proxyHost.isEmpty())
    {
        QString proxy;
        if (!profile.proxyType.isEmpty())
            proxy += profile.proxyType + QStringLiteral("://");
        if (!profile.proxyUsername.isEmpty())
        {
            proxy += profile.proxyUsername;
            if (!profile.proxyPassword.isEmpty())
                proxy += QLatin1Char(':') + profile.proxyPassword;
            proxy += QLatin1Char('@');
        }
        proxy += profile.proxyHost;
        if (profile.proxyPort > 0)
            proxy += QStringLiteral(":%1").arg(profile.proxyPort);
        result << QStringLiteral("/proxy:%1").arg(proxy);
    }

    if (profile.dynamicResolution)
        result << QStringLiteral("/dynamic-resolution");
    else if ((profile.desktopWidth > 0) && (profile.desktopHeight > 0))
        result << QStringLiteral("/size:%1x%2").arg(profile.desktopWidth).arg(profile.desktopHeight);

    if (profile.spanMonitors)
        result << QStringLiteral("+span");
    if ((profile.scaleFactor > 0) && (profile.scaleFactor != 100))
        result << QStringLiteral("/scale:%1").arg(profile.scaleFactor);
    if (profile.extraOptions.value(QStringLiteral("smartSizing"), false).toBool())
        result << QStringLiteral("+smart-sizing");
    const int orientation = profile.extraOptions.value(QStringLiteral("orientation"), 0).toInt();
    if (orientation > 0)
        result << QStringLiteral("/orientation:%1").arg(orientation);

    // En GNOME/Wayland el toggle de pantalla completa de FreeRDP es poco fiable
    // (Mutter reatrapa la ventana, FreeRDP #10993 / #11737). En ese caso QtRDP
    // gestiona el fullscreen por EWMH y el atajo global, así que no se pasa /f
    // ni la floatbar (su botón usaría el camino defectuoso).
    const bool externalFullscreen =
        profile.extraOptions.value(QStringLiteral("externalFullscreen"), false).toBool();

    // Pantalla completa al iniciar (WinBoat abre en fullscreen; por omisión,
    // ventana normal y <Ctrl>+<Alt>+<Enter> alterna el modo).
    if (!externalFullscreen && profile.extraOptions.value(QStringLiteral("fullScreen"), false).toBool())
        result << QStringLiteral("/f");

    // Barra de conexión azul pineable, como mstsc/WinBoat.
    // sticky:on la fija (no se autooculta), default:visible la muestra siempre
    // y show:always la mantiene visible en ventana y fullscreen. Se usa
    // show:always (y no show:fullscreen) porque en Wayland/XWayland el toggle
    // de pantalla completa con show:fullscreen se atasca tras el primer uso
    // (FreeRDP #10993 / #11737): al restaurar, la imagen queda en fullscreen y
    // la barra desaparece. show:always es el workaround documentado.
    if (!externalFullscreen &&
        profile.extraOptions.value(QStringLiteral("showConnectionBar"), true).toBool())
        result << QStringLiteral("/floatbar:sticky:on,default:visible,show:always");

    // Experiencia remota (equivalen a las casillas de mstsc). Se usan formas
    // explícitas +toggle/-toggle para que el resultado dependa del perfil.
    if (profile.wallpaper == QLatin1String("0"))
        result << QStringLiteral("-wallpaper");
    result << (profile.themesEnabled ? QStringLiteral("+themes") : QStringLiteral("-themes"));
    result << (profile.fontSmoothing ? QStringLiteral("+fonts") : QStringLiteral("-fonts"));
    result << (profile.desktopComposition ? QStringLiteral("+aero") : QStringLiteral("-aero"));
    result << (profile.animations ? QStringLiteral("+menu-anims") : QStringLiteral("-menu-anims"));

    if (!profile.networkType.isEmpty())
        result << QStringLiteral("/network:%1").arg(profile.networkType);

    const bool bitmapCache =
        profile.extraOptions.value(QStringLiteral("bitmapCache"), true).toBool();
    result << QStringLiteral("/cache:bitmap:%1")
                  .arg(bitmapCache ? QStringLiteral("on") : QStringLiteral("off"));

    const bool autoReconnect =
        profile.extraOptions.value(QStringLiteral("autoReconnect"), true).toBool();
    result << (autoReconnect ? QStringLiteral("+auto-reconnect")
                             : QStringLiteral("-auto-reconnect"));
    const int reconnectRetries =
        profile.extraOptions.value(QStringLiteral("autoReconnectMaxRetries"), -1).toInt();
    if (reconnectRetries >= 0)
        result << QStringLiteral("/auto-reconnect-max-retries:%1").arg(reconnectRetries);

    if (profile.extraOptions.value(QStringLiteral("windowDrag"), false).toBool())
        result << QStringLiteral("+window-drag");

    const int compressionLevel =
        profile.extraOptions.value(QStringLiteral("compressionLevel"), -1).toInt();
    if (compressionLevel >= 0)
        result << QStringLiteral("/compression-level:%1").arg(compressionLevel);

    if (profile.extraOptions.value(QStringLiteral("rfx"), false).toBool())
        result << QStringLiteral("+rfx");
    if (profile.extraOptions.value(QStringLiteral("nsc"), false).toBool())
        result << QStringLiteral("+nsc");
    if (profile.extraOptions.value(QStringLiteral("jpeg"), false).toBool())
        result << QStringLiteral("+jpeg");
    if (profile.extraOptions.value(QStringLiteral("video"), false).toBool())
        result << QStringLiteral("+video");

    const QString gdi = profile.extraOptions.value(QStringLiteral("gdi")).toString();
    if (gdi == QLatin1String("sw") || gdi == QLatin1String("hw"))
        result << QStringLiteral("/gdi:%1").arg(gdi);

    if (profile.redirectClipboard)
        result << (profile.redirectClipboardFiles
                       ? QStringLiteral("/clipboard")
                       : QStringLiteral("/clipboard:files-to:off"));
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

    if (profile.extraOptions.value(QStringLiteral("redirectSmartCards"), false).toBool())
        result << QStringLiteral("/smartcard");
    if (profile.extraOptions.value(QStringLiteral("multitouch"), false).toBool())
        result << QStringLiteral("+multitouch");

    // «Aplicar combinaciones de teclas»: solo «en el equipo remoto» difiere del
    // comportamiento por omisión (que captura el teclado localmente).
    if (profile.extraOptions.value(QStringLiteral("keyboardHook")).toString() ==
        QLatin1String("remote"))
        result << QStringLiteral("-grab-keyboard");

    if (profile.useMultimon)
        result << QStringLiteral("/multimon");
    if (profile.gatewayEnabled && !profile.gatewayHostname.isEmpty())
    {
        result << QStringLiteral("/g:%1").arg(profile.gatewayHostname);
        // Con «mismas credenciales» no se envía usuario de puerta de enlace:
        // FreeRDP reutiliza entonces las de la sesión.
        if (!profile.gatewaySameCredentials && !profile.gatewayUsername.isEmpty())
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

qint64 XFreeRdpLauncher::processId() const
{
    return m_process ? m_process->processId() : 0;
}

} // namespace qtrdp
