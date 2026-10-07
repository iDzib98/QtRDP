#include "core/ConnectionProfile.h"
#include "core/CredentialFormat.h"
#include "core/CredentialStore.h"
#include "core/ProfileStore.h"
#include "core/RdpFile.h"
#include "core/SessionRegistry.h"
#include "core/XFreeRdpLauncher.h"
#include "core/backends/rdp/RdpSession.h"

#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

using namespace qtrdp;

namespace {

struct LiveTarget
{
    QByteArray host;
    quint16 port = 3389;
    int width = 1024;
    int height = 768;
    bool valid = false;
};

LiveTarget liveTarget()
{
    LiveTarget target;
    target.host = qgetenv("QTRDP_TEST_RDP_HOST");
    if (target.host.isEmpty())
        return target;

    bool ok = false;
    const int port = qEnvironmentVariableIntValue("QTRDP_TEST_RDP_PORT", &ok);
    if (ok && port > 0)
        target.port = static_cast<quint16>(port);

    const int width = qEnvironmentVariableIntValue("QTRDP_TEST_RDP_WIDTH");
    const int height = qEnvironmentVariableIntValue("QTRDP_TEST_RDP_HEIGHT");
    if (width > 0)
        target.width = width;
    if (height > 0)
        target.height = height;

    target.valid = true;
    return target;
}

ConnectionProfile profileFor(const LiveTarget& target)
{
    ConnectionProfile profile;
    profile.host = QString::fromLatin1(target.host);
    profile.port = target.port;
    profile.desktopWidth = target.width;
    profile.desktopHeight = target.height;

    // QTRDP_TEST_DRIVES="home,/home/x;/media" para probar unidades: cada entrada
    // separada por «;» usa la forma nombre,ruta de xfreerdp3.
    const QByteArray drives = qgetenv("QTRDP_TEST_DRIVES");
    if (!drives.isEmpty())
        profile.drives = QString::fromUtf8(drives).split(';', Qt::SkipEmptyParts);

    return profile;
}

SessionBackend::Credentials liveCredentials()
{
    SessionBackend::Credentials credentials;
    credentials.username = QString::fromUtf8(qgetenv("QTRDP_TEST_RDP_USER"));
    if (credentials.username.isEmpty())
        credentials.username = QStringLiteral("prueba");
    credentials.password = QString::fromUtf8(qgetenv("QTRDP_TEST_RDP_PASSWORD"));
    credentials.domain = QString::fromUtf8(qgetenv("QTRDP_TEST_RDP_DOMAIN"));
    return credentials;
}

// Vuelca los mensajes del panel de registro cuando se pide con
// QTRDP_TEST_VERBOSE=1 (útil para depurar canales en un servidor real).
void dumpLogs(const QSignalSpy& logSpy)
{
    if (qEnvironmentVariableIntValue("QTRDP_TEST_VERBOSE") != 1)
        return;

    // Los mensajes se entregan en cola: hay que vaciar el bucle de eventos.
    QTest::qWait(50);

    for (const QList<QVariant>& entry : logSpy)
        fprintf(stderr, "[log] %s\n", qPrintable(entry.at(0).toString()));
}

} // namespace

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    void capabilitiesFollowProfile()
    {
        RdpSession session;

        // Perfil por defecto: resolución dinámica activa, sin unidades.
        QList<SessionCapability> caps = session.capabilities();
        QVERIFY(caps.contains(SessionCapability::DynamicResolution));
        QVERIFY(!caps.contains(SessionCapability::DriveRedirection));
        QVERIFY(!caps.contains(SessionCapability::Printing));

        // Con unidad compartida e impresora, ambas aparecen.
        ConnectionProfile profile;
        profile.host = QStringLiteral("127.0.0.1");
        profile.port = 1;
        profile.dynamicResolution = false;
        profile.drives = QStringList{ QStringLiteral("home,/home/prueba") };
        profile.redirectPrinters = true;

        SessionBackend::Credentials credentials = liveCredentials();

        const bool started = session.start(profile, credentials);
        if (started)
        {
            const QList<SessionCapability> withDevices = session.capabilities();
            QVERIFY(!withDevices.contains(SessionCapability::DynamicResolution));
            QVERIFY(withDevices.contains(SessionCapability::DriveRedirection));
            QVERIFY(withDevices.contains(SessionCapability::Printing));
            session.requestStop();
        }
    }

    void xfreerdpErrorAnalysis()
    {
        // Contraseña incorrecta.
        auto err1 = XFreeRdpLauncher::analyzeOutput(
            "ERRCONNECT_LOGON_FAILURE [0x00020014]", 1);
        QCOMPARE(err1.title, QStringLiteral("Contraseña incorrecta"));
        QVERIFY(err1.isFatal);
        QVERIFY(err1.friendlyMessage.contains(QStringLiteral("contraseña")));

        // Desconexión normal.
        auto err2 = XFreeRdpLauncher::analyzeOutput(
            "ERRINFO_LOGOFF_BY_USER", 12);
        QVERIFY(!err2.isFatal);

        // Equipo no encontrado.
        auto err3 = XFreeRdpLauncher::analyzeOutput(
            "ERRCONNECT_DNS_NAME_NOT_FOUND", 1);
        QCOMPARE(err3.title, QStringLiteral("Equipo no encontrado"));
        QVERIFY(err3.isFatal);

        // Caída inesperada (SIGABRT).
        auto err4 = XFreeRdpLauncher::analyzeOutput("some output", 134);
        QCOMPARE(err4.title, QStringLiteral("Error interno"));
        QVERIFY(err4.isFatal);

        // No se pudo conectar.
        auto err5 = XFreeRdpLauncher::analyzeOutput(
            "ERRCONNECT_CONNECT_FAILED", 1);
        QCOMPARE(err5.title, QStringLiteral("No se pudo conectar"));
        QVERIFY(err5.isFatal);

        // Dominio Kerberos.
        auto err6 = XFreeRdpLauncher::analyzeOutput(
            "Cannot find KDC for realm \"WORKGROUP\"", 1);
        QCOMPARE(err6.title, QStringLiteral("Dominio no disponible"));
        QVERIFY(err6.isFatal);
    }

    void registryContainsRdp()
    {
        QVERIFY(SessionRegistry::isSupported(QStringLiteral("rdp")));
        QCOMPARE(SessionRegistry::displayName(QStringLiteral("rdp")), QStringLiteral("RDP"));
        QVERIFY(SessionRegistry::create(QStringLiteral("rdp"), nullptr) != nullptr);
        QVERIFY(SessionRegistry::create(QStringLiteral("vnc"), nullptr) == nullptr);
    }

    void xfreerdpArgumentsMatchProfile()
    {
        ConnectionProfile profile;
        profile.host = QStringLiteral("10.0.0.5");
        profile.port = 3390;
        profile.username = QStringLiteral("soporte");
        profile.domain = QStringLiteral("LAB");
        profile.dynamicResolution = true;
        profile.redirectClipboard = true;
        profile.redirectAudio = true;
        profile.redirectMicrophone = true;
        profile.redirectHomeDrive = true;
        profile.drives = { QStringLiteral("home,/home/soporte") };

        SessionBackend::Credentials credentials;
        credentials.username = profile.username;
        credentials.domain = profile.domain;
        credentials.password = QStringLiteral("secreto");

        const QStringList args = XFreeRdpLauncher::arguments(profile, credentials);
        QVERIFY(args.contains(QStringLiteral("/v:10.0.0.5:3390")));
        QVERIFY(args.contains(QStringLiteral("/u:soporte")));
        QVERIFY(args.contains(QStringLiteral("/d:LAB")));
        QVERIFY(args.contains(QStringLiteral("/p:secreto")));
        QVERIFY(args.contains(QStringLiteral("/dynamic-resolution")));
        QVERIFY(args.contains(QStringLiteral("/clipboard")));
        QVERIFY(args.contains(QStringLiteral("/sound")));
        QVERIFY(args.contains(QStringLiteral("/microphone")));
        QVERIFY(args.contains(QStringLiteral("/drive:home,/home/soporte")));
    }

    void profileRoundTrip()
    {
        ConnectionProfile profile;
        profile.host = QStringLiteral("10.0.0.5");
        profile.port = 3390;
        profile.username = QStringLiteral("soporte");
        profile.domain = QStringLiteral("LAB");
        profile.redirectDrives = true;
        profile.drives = { QStringLiteral("home"), QStringLiteral("temp") };
        profile.desktopWidth = 1600;
        profile.desktopHeight = 900;

        const ConnectionProfile restored =
	        ConnectionProfile::fromVariantMap(profile.toVariantMap());

        QCOMPARE(restored.host, profile.host);
        QCOMPARE(restored.port, profile.port);
        QCOMPARE(restored.username, profile.username);
        QCOMPARE(restored.domain, profile.domain);
        QCOMPARE(restored.redirectDrives, profile.redirectDrives);
        QCOMPARE(restored.drives, profile.drives);
        QCOMPARE(restored.desktopWidth, profile.desktopWidth);
        QCOMPARE(restored.desktopHeight, profile.desktopHeight);
        QVERIFY(restored.isValid());
        QCOMPARE(restored.displayName(), QStringLiteral("LAB\\soporte"));

        ConnectionProfile soloUsuario;
        soloUsuario.host = QStringLiteral("equipo");
        soloUsuario.username = QStringLiteral("soporte");
        QCOMPARE(soloUsuario.displayName(), QStringLiteral("soporte"));

        soloUsuario.name = QStringLiteral("Servidor de pruebas");
        QCOMPARE(soloUsuario.displayName(), QStringLiteral("Servidor de pruebas"));
    }

    void liveSessionStreamsFrames()
    {
        const LiveTarget target = liveTarget();
        if (!target.valid)
            QSKIP("define QTRDP_TEST_RDP_HOST para probar contra un servidor RDP real");

        RdpSession session;
        QSignalSpy connectedSpy(&session, &SessionBackend::connected);
        QSignalSpy errorSpy(&session, &SessionBackend::error);
        QSignalSpy logSpy(&session, &SessionBackend::logMessage);
        QSignalSpy frameSpy(&session, &SessionBackend::frameReady);
        QSignalSpy frameSizeSpy(&session, &SessionBackend::frameSizeChanged);

        SessionBackend::Credentials credentials = liveCredentials();

        QVERIFY(session.start(profileFor(target), credentials));

        if (!connectedSpy.wait(30000))
        {
            const SessionError failure =
	            errorSpy.isEmpty() ? SessionError() : errorSpy.at(0).at(0).value<SessionError>();
            QVERIFY2(!failure.message.isEmpty(),
                     qPrintable(QStringLiteral("la conexión falló sin diagnóstico")));
            return;
        }

        QCOMPARE(session.state(), SessionState::Connected);
        QCOMPARE(session.hostName(), QString::fromLatin1(target.host));
        QCOMPARE(session.userName(), QStringLiteral("prueba"));
        QVERIFY(session.capabilities().contains(SessionCapability::DynamicResolution));

        dumpLogs(logSpy);

        // El servidor puede usar RDPEGFX o BitmapUpdate: ambos deben publicar.
        bool rendererReady = false;
        for (int attempt = 0; attempt < 40 && !rendererReady; ++attempt)
        {
            QTest::qWait(250);
            for (const QList<QVariant>& entry : logSpy)
            {
                const QString message = entry.at(0).toString();
                if (message.contains(QStringLiteral("RDPEGFX")) ||
                    message.contains(QStringLiteral("BitmapUpdate")))
                    rendererReady = true;
            }
        }
        QVERIFY2(rendererReady, "ningún camino de vídeo quedó activo");

        QVERIFY(frameSpy.count() > 0 || frameSpy.wait(30000));
        const QImage frame = frameSpy.last().at(0).value<QImage>();
        QCOMPARE(frame.size(), QSize(target.width, target.height));

        QVERIFY(frameSizeSpy.count() > 0 || frameSizeSpy.wait(5000));
        QCOMPARE(frameSizeSpy.last().at(0).toSize(), frame.size());

        session.requestStop();
        QCOMPARE(session.state(), SessionState::Disconnected);
    }

    void liveSessionDeliversFrames()
    {
        const LiveTarget target = liveTarget();
        if (!target.valid)
            QSKIP("define QTRDP_TEST_RDP_HOST para probar contra un servidor RDP real");
        if (qEnvironmentVariableIntValue("QTRDP_TEST_REQUIRE_PIXELS") != 1)
            QSKIP("define QTRDP_TEST_REQUIRE_PIXELS=1 para exigir píxeles distintos de negro");

        RdpSession session;
        QSignalSpy frameSpy(&session, &SessionBackend::frameReady);
        QSignalSpy connectedSpy(&session, &SessionBackend::connected);
        QSignalSpy errorSpy(&session, &SessionBackend::error);
        QSignalSpy logSpy(&session, &SessionBackend::logMessage);

        ConnectionProfile profile = profileFor(target);
        SessionBackend::Credentials credentials = liveCredentials();

        QVERIFY(session.start(profile, credentials));

        if (!connectedSpy.wait(30000))
        {
            const SessionError failure =
	            errorSpy.isEmpty() ? SessionError() : errorSpy.at(0).at(0).value<SessionError>();
            QVERIFY2(!failure.message.isEmpty(),
                     qPrintable(QStringLiteral("la conexión falló sin diagnóstico")));
            return;
        }

        QCOMPARE(session.state(), SessionState::Connected);
        dumpLogs(logSpy);
        QVERIFY(frameSpy.count() > 0 || frameSpy.wait(30000));

        const QByteArray dumpDir = qgetenv("QTRDP_TEST_DUMP_DIR");
        if (!dumpDir.isEmpty())
            frameSpy.last().at(0).value<QImage>().save(
	            QString::fromLocal8Bit(dumpDir) + QStringLiteral("/ultimo-frame.png"));

        QSize frameSize;
        QSet<QRgb> colors;
        for (int attempt = 0; attempt < 60 && (frameSpy.count() < 3 || colors.size() < 2); ++attempt)
        {
            QTest::qWait(250);
            const QList<QVariant> latest = frameSpy.last().toList();
            if (latest.isEmpty())
                continue;
            const QImage frame = latest.first().value<QImage>();
            frameSize = frame.size();
            if (frameSize.isEmpty())
                continue;
            for (int y = 0; y < frame.height(); y += 8)
            {
                for (int x = 0; x < frame.width(); x += 8)
                    colors.insert(frame.pixel(x, y));
            }
        }

        QCOMPARE(frameSize, QSize(profile.desktopWidth, profile.desktopHeight));
        QVERIFY2(colors.size() > 1, "el escritorio remoto llegó completamente negro");
        QCOMPARE(session.desktopSize(), frameSize);

        session.requestStop();
        QCOMPARE(session.state(), SessionState::Disconnected);
    }

    void liveSessionResizesDesktop()
    {
        const LiveTarget target = liveTarget();
        const int width = qEnvironmentVariableIntValue("QTRDP_TEST_RESIZE_WIDTH");
        const int height = qEnvironmentVariableIntValue("QTRDP_TEST_RESIZE_HEIGHT");
        if (!target.valid || (width <= 0) || (height <= 0))
            QSKIP("define host, credenciales y QTRDP_TEST_RESIZE_WIDTH/HEIGHT para probar DISP");

        RdpSession session;
        QSignalSpy connectedSpy(&session, &SessionBackend::connected);
        QSignalSpy errorSpy(&session, &SessionBackend::error);
        QSignalSpy frameSizeSpy(&session, &SessionBackend::frameSizeChanged);
        QSignalSpy logSpy(&session, &SessionBackend::logMessage);

        QVERIFY(session.start(profileFor(target), liveCredentials()));
        if (!connectedSpy.wait(30000))
        {
            const SessionError failure =
                errorSpy.isEmpty() ? SessionError() : errorSpy.at(0).at(0).value<SessionError>();
            QVERIFY2(!failure.message.isEmpty(), "la conexión de redimensionado no dio diagnóstico");
            return;
        }

        const QSize requested(width, height);
        // Deja que CAPS y el primer framebuffer terminen antes de probar el
        // cambio, igual que el usuario que redimensiona una sesión ya visible.
        QTest::qWait(2000);
        session.requestDesktopSize(requested);

        bool applied = false;
        for (int attempt = 0; attempt < 120 && !applied; ++attempt)
        {
            QTest::qWait(250);
            for (const QList<QVariant>& entry : frameSizeSpy)
            {
                if (entry.at(0).toSize() == requested)
                {
                    applied = true;
                    break;
                }
            }
        }

        dumpLogs(logSpy);
        QVERIFY2(applied, qPrintable(QStringLiteral(
            "Windows no confirmó el tamaño solicitado %1x%2").arg(width).arg(height)));
        session.requestStop();
        QCOMPARE(session.state(), SessionState::Disconnected);
    }

    void readsMstscRdpFile()
    {
        const QString text = QStringLiteral(
	    "; ejemplo generado por mstsc\r\n"
	    "screen mode id:2\r\n"
	    "desktopwidth:1920\r\n"
	    "desktopheight:1080\r\n"
	    "color depth:32\r\n"
	    "desktop scale factor:125\r\n"
	    "smart sizing:1\r\n"
	    "use multimon:0\r\n"
	    "full address:10.0.0.9:3391\r\n"
	    "user name:LAB\\soporte\r\n"
	    "domain:LAB\r\n"
	    "redirectclipboard:1\r\n"
	    "audiomode:2\r\n"
	    "redirectdrives:1\r\n"
	    "redirect home drive:1\r\n"
	    "drivestoredirect:C:;D:\r\n"
	    "allow themes:1\r\n"
	    "allow font smoothing:0\r\n"
	    "allow desktop composition:0\r\n"
	    "animate menu and windows:1\r\n"
	    "gateway enabled:1\r\n"
	    "gatewayhostname:gw.lab.local\r\n"
	    "gatewayport:443\r\n"
	    "alternate shell:powershell.exe\r\n"
	    "wallpaper:C:\\Windows\\Web\\Wallpaper\\Windows\\img0.jpg\r\n");

        const ConnectionProfile profile = RdpFile::fromText(text);

        QCOMPARE(profile.host, QStringLiteral("10.0.0.9"));
        QCOMPARE(profile.port, quint16(3391));
        QCOMPARE(profile.username, QStringLiteral("LAB\\soporte"));
        QCOMPARE(profile.domain, QStringLiteral("LAB"));
        QCOMPARE(profile.desktopWidth, 1920);
        QCOMPARE(profile.desktopHeight, 1080);
        QCOMPARE(profile.colorDepth, 32);
        QCOMPARE(profile.scaleFactor, 125);
        QVERIFY(profile.dynamicResolution);
        QVERIFY(!profile.useMultimon);
        QVERIFY(profile.redirectClipboard);
        QVERIFY(profile.redirectAudio);
        QVERIFY(profile.redirectMicrophone);
        QVERIFY(profile.redirectDrives);
        QVERIFY(profile.redirectHomeDrive);
        QCOMPARE(profile.drives, QStringList({ QStringLiteral("C"), QStringLiteral("D") }));
        QVERIFY(profile.themesEnabled);
        QVERIFY(!profile.fontSmoothing);
        QVERIFY(!profile.desktopComposition);
        QVERIFY(profile.animations);
        QVERIFY(profile.gatewayEnabled);
        QCOMPARE(profile.gatewayHostname, QStringLiteral("gw.lab.local"));
        QCOMPARE(profile.gatewayPort, 443);
        QCOMPARE(profile.shell, QStringLiteral("powershell.exe"));
        QCOMPARE(profile.wallpaper, QStringLiteral("C:\\Windows\\Web\\Wallpaper\\Windows\\img0.jpg"));
        QVERIFY(profile.isValid());
    }

    void rdpFileRoundTripKeepsSettings()
    {
        ConnectionProfile profile;
        profile.id = QStringLiteral("laboratorio");
        profile.name = QStringLiteral("Laboratorio");
        profile.host = QStringLiteral("10.0.0.9");
        profile.port = 3391;
        profile.username = QStringLiteral("soporte");
        profile.domain = QStringLiteral("LAB");
        profile.desktopWidth = 1600;
        profile.desktopHeight = 900;
        profile.scaleFactor = 150;
        profile.redirectDrives = true;
        profile.redirectHomeDrive = true;
        profile.drives = { QStringLiteral("home"), QStringLiteral("temp") };
        profile.redirectAudio = true;
        profile.redirectMicrophone = true;
        profile.redirectPrinters = true;
        profile.gatewayEnabled = true;
        profile.gatewayHostname = QStringLiteral("gw.lab.local");
        profile.gatewayPort = 443;
        profile.fontSmoothing = false;
        profile.shell = QStringLiteral("cmd.exe");

        const ConnectionProfile restored = RdpFile::fromText(RdpFile::toText(profile));

        QCOMPARE(restored.host, profile.host);
        QCOMPARE(restored.port, profile.port);
        QCOMPARE(restored.username, profile.username);
        QCOMPARE(restored.domain, profile.domain);
        QCOMPARE(restored.desktopWidth, profile.desktopWidth);
        QCOMPARE(restored.desktopHeight, profile.desktopHeight);
        QCOMPARE(restored.scaleFactor, profile.scaleFactor);
        QCOMPARE(restored.redirectDrives, profile.redirectDrives);
        QCOMPARE(restored.redirectHomeDrive, profile.redirectHomeDrive);
        QCOMPARE(restored.redirectAudio, profile.redirectAudio);
        QCOMPARE(restored.redirectMicrophone, profile.redirectMicrophone);
        QCOMPARE(restored.redirectPrinters, profile.redirectPrinters);
        QCOMPARE(restored.drives, profile.drives);
        QCOMPARE(restored.gatewayHostname, profile.gatewayHostname);
        QCOMPARE(restored.fontSmoothing, profile.fontSmoothing);
        QCOMPARE(restored.shell, profile.shell);
        // El archivo .rdp no guarda el nombre del perfil, solo los ajustes de conexión.
        QCOMPARE(restored.displayName(), QStringLiteral("LAB\\soporte"));
    }

    void rdpFileUsesHostWhenPortIsDefault()
    {
        ConnectionProfile profile;
        profile.host = QStringLiteral("10.0.0.5");
        const QString text = RdpFile::toText(profile);
        QVERIFY(text.contains(QStringLiteral("full address:10.0.0.5\r\n")));

        profile.port = 3390;
        QVERIFY(RdpFile::toText(profile)
	                .contains(QStringLiteral("full address:10.0.0.5:3390")));
    }

    void rdpFileReadsFromDisk()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString path = dir.path() + QStringLiteral("/escritorio.rdp");
        ConnectionProfile profile;
        profile.host = QStringLiteral("192.168.1.20");
        profile.desktopWidth = 1280;
        profile.desktopHeight = 1024;
        QVERIFY(RdpFile::write(path, profile));

        const ConnectionProfile loaded = RdpFile::read(path);
        QCOMPARE(loaded.host, QStringLiteral("192.168.1.20"));
        QCOMPARE(loaded.desktopWidth, 1280);
        QCOMPARE(loaded.desktopHeight, 1024);
        QCOMPARE(loaded.rdpFilePath, path);
        QCOMPARE(loaded.name, QStringLiteral("escritorio"));
        QVERIFY(loaded.isValid());

        QVERIFY(!RdpFile::write(dir.path() + QStringLiteral("/inexistente/x.rdp"), profile));
        QVERIFY(!RdpFile::read(dir.path() + QStringLiteral("/no-existe.rdp")).isValid());
    }

    void profileStorePersistsAcrossInstances()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/perfiles.ini");

        ConnectionProfile first;
        first.name = QStringLiteral("Escritorio");
        first.host = QStringLiteral("10.0.0.5");
        first.username = QStringLiteral("soporte");
        first.domain = QStringLiteral("LAB");

        ConnectionProfile second;
        second.name = QStringLiteral("Servidor");
        second.host = QStringLiteral("10.0.0.6");
        second.port = 3390;
        second.rememberPassword = true;

        {
            ProfileStore store(path);
            QCOMPARE(store.count(), 0);
            QVERIFY(store.add(first));
            QVERIFY(store.add(second));
            QCOMPARE(store.count(), 2);

            // Un perfil todavía sin equipo se guarda como plantilla, pero no
            // se puede conectar con él.
            ConnectionProfile incomplete;
            incomplete.name = QStringLiteral("Plantilla");
            QVERIFY(store.add(incomplete));
            QVERIFY(!store.profiles().at(2).isValid());
            QVERIFY(store.remove(store.profiles().at(2).id));
        }

        ProfileStore reloaded(path);
        QCOMPARE(reloaded.count(), 2);
        QCOMPARE(reloaded.profiles().at(0).name, QStringLiteral("Escritorio"));
        QCOMPARE(reloaded.profiles().at(1).name, QStringLiteral("Servidor"));
        QCOMPARE(reloaded.profiles().at(1).port, quint16(3390));

        const QString id = reloaded.profiles().at(1).id;
        QVERIFY(!id.isEmpty());
        QVERIFY(reloaded.contains(id));

        ConnectionProfile edited = reloaded.profile(id);
        edited.host = QStringLiteral("10.0.0.7");
        QVERIFY(reloaded.update(edited));
        QCOMPARE(ProfileStore(path).profile(id).host, QStringLiteral("10.0.0.7"));

        QVERIFY(!reloaded.update(ConnectionProfile()));
        QVERIFY(reloaded.remove(id));
        QVERIFY(!reloaded.remove(id));
        QCOMPARE(ProfileStore(path).count(), 1);
    }

    void profileStoreGeneratesIdsWhenMissingOrDuplicated()
    {
        QTemporaryDir dir;
        ProfileStore store(dir.path() + QStringLiteral("/perfiles.ini"));

        ConnectionProfile profile;
        profile.name = QStringLiteral("Sin id");
        profile.host = QStringLiteral("10.0.0.8");
        QVERIFY(store.add(profile));
        const QString generated = store.profiles().at(0).id;
        QVERIFY(!generated.isEmpty());

        ConnectionProfile duplicated = profile;
        duplicated.id = generated;
        QVERIFY(store.add(duplicated));
        QVERIFY(store.profiles().at(1).id != generated);
    }

    void profileStoreKeepsManualOrder()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/perfiles.ini");

        ProfileStore store(path);
        for (int index = 0; index < 3; ++index)
        {
            ConnectionProfile profile;
            profile.name = QStringLiteral("P%1").arg(index);
            profile.host = QStringLiteral("10.0.0.%1").arg(index + 1);
            QVERIFY(store.add(profile));
        }

        const QString last = store.profiles().at(2).id;
        QVERIFY(store.move(last, -1));
        QCOMPARE(ProfileStore(path).profiles().at(1).name, QStringLiteral("P2"));
        QVERIFY(!store.move(last, -5));
        QVERIFY(store.move(last, 1));
        QCOMPARE(ProfileStore(path).profiles().at(2).name, QStringLiteral("P2"));
    }

    void profileStoreImportsAndExportsRdpFiles()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/perfiles.ini");
        const QString rdpPath = dir.path() + QStringLiteral("/servidor.rdp");

        ConnectionProfile source;
        source.name = QStringLiteral("Servidor");
        source.host = QStringLiteral("10.0.0.9");
        source.port = 3391;
        source.username = QStringLiteral("soporte");
        source.desktopWidth = 1600;
        source.desktopHeight = 900;
        QVERIFY(RdpFile::write(rdpPath, source));

        ProfileStore store(path);
        QVERIFY(store.importRdpFile(rdpPath));
        QCOMPARE(store.count(), 1);
        QCOMPARE(store.profiles().at(0).host, QStringLiteral("10.0.0.9"));
        QCOMPARE(store.profiles().at(0).port, quint16(3391));
        QCOMPARE(store.profiles().at(0).name, QStringLiteral("servidor"));
        QCOMPARE(store.profiles().at(0).rdpFilePath, rdpPath);

        // Reimportar el mismo archivo actualiza el perfil en lugar de duplicarlo.
        QVERIFY(store.importRdpFile(rdpPath));
        QCOMPARE(store.count(), 1);

        const QString exported = dir.path() + QStringLiteral("/copia.rdp");
        QVERIFY(store.exportRdpFile(store.profiles().at(0).id, exported));
        QCOMPARE(RdpFile::read(exported).host, QStringLiteral("10.0.0.9"));
        QVERIFY(!store.exportRdpFile(QStringLiteral("inexistente"), exported));
        QVERIFY(!store.importRdpFile(dir.path() + QStringLiteral("/no-existe.rdp")));
    }

    void credentialsAreSplitIntoUserAndDomain()
    {
        // Lo que escribe la gente en el cuadro «Usuario».
        QCOMPARE(normalizeCredentials(QStringLiteral("soporte"), QString()).username,
                 QStringLiteral("soporte"));
        QVERIFY(normalizeCredentials(QStringLiteral("soporte"), QString()).domain.isEmpty());

        QCOMPARE(normalizeCredentials(QStringLiteral("LAB\\soporte"), QString()).username,
                 QStringLiteral("soporte"));
        QCOMPARE(normalizeCredentials(QStringLiteral("LAB\\soporte"), QString()).domain,
                 QStringLiteral("LAB"));

        // Cuenta local del propio equipo: el dominio no se envía.
        const NormalizedCredentials local =
	    normalizeCredentials(QStringLiteral(".\\soporte"), QString());
        QCOMPARE(local.username, QStringLiteral("soporte"));
        QVERIFY(local.domain.isEmpty());

        // UPN.
        const NormalizedCredentials upn =
	    normalizeCredentials(QStringLiteral("soporte@lab.local"), QString());
        QCOMPARE(upn.username, QStringLiteral("soporte"));
        QCOMPARE(upn.domain, QStringLiteral("lab.local"));

        // Si el dominio viene aparte, manda sobre el del usuario.
        const NormalizedCredentials both =
	    normalizeCredentials(QStringLiteral("OTRO\\soporte"), QStringLiteral("LAB"));
        QCOMPARE(both.username, QStringLiteral("soporte"));
        QCOMPARE(both.domain, QStringLiteral("LAB"));

        // Espacios sobrantes y casos raros.
        QCOMPARE(normalizeCredentials(QStringLiteral("  soporte  "), QStringLiteral(" LAB ")).username,
                 QStringLiteral("soporte"));
        QCOMPARE(normalizeCredentials(QStringLiteral("  soporte  "), QStringLiteral(" LAB ")).domain,
                 QStringLiteral("LAB"));
        // Entrada mal formada con dos barras: el dominio nunca puede llevar una,
        // así que se toma lo que hay antes de la primera.
        QCOMPARE(normalizeCredentials(QStringLiteral("LAB\\dominio\\soporte"), QString()).username,
                 QStringLiteral("soporte"));
        QCOMPARE(normalizeCredentials(QStringLiteral("LAB\\dominio\\soporte"), QString()).domain,
                 QStringLiteral("LAB"));
        QVERIFY(normalizeCredentials(QString(), QString()).username.isEmpty());
    }

    void credentialIdIsStableAndCaseInsensitive()
    {
        ConnectionProfile profile;
        profile.host = QStringLiteral("Servidor.LAB.local");
        profile.port = 3391;
        profile.username = QStringLiteral("Soporte");
        profile.domain = QStringLiteral("LAB");

        const QString id = CredentialStore::credentialId(profile);
        QCOMPARE(id, QStringLiteral("servidor.lab.local|3391|soporte|lab"));

        profile.host = QStringLiteral("SERVIDOR.LAB.LOCAL");
        profile.username = QStringLiteral("SOPORTE");
        QCOMPARE(CredentialStore::credentialId(profile), id);
    }

    void credentialStoreSavesAndClearsSecrets()
    {
        CredentialStore::setInstance(CredentialStore::createFileStore());
        CredentialStore* store = CredentialStore::instance();
        QVERIFY(store != nullptr);
        QVERIFY(store->isAvailable());
        QVERIFY(!store->backendName().isEmpty());

        const QString id = QStringLiteral("10.0.0.9|3389|soporte|lab");
        const QString secret =
	    QStringLiteral("contraseña con espacios, acentos y símbolos: ñáé#123");

        QString error;
        QVERIFY2(store->write(id, secret, &error), qPrintable(error));
        QCOMPARE(store->read(id), secret);
        QCOMPARE(store->read(id, &error), secret);
        QVERIFY(error.isEmpty());

        // Otro equipo o usuario no ve la contraseña.
        QVERIFY(store->read(QStringLiteral("10.0.0.10|3389|soporte|lab")).isEmpty());

        QVERIFY(store->write(id, QStringLiteral("otra"), &error));
        QCOMPARE(store->read(id), QStringLiteral("otra"));

        QVERIFY(store->remove(id, &error));
        QVERIFY(store->read(id).isEmpty());
        QVERIFY(store->remove(id, &error));

        QVERIFY(store->write(id, secret, &error));
        QVERIFY(store->remove(QStringLiteral("desconocida"), &error));
        QCOMPARE(store->read(id), secret);
        store->remove(id);
    }

    void credentialStoreKeepsPlainFilePrivate()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());

        // Aísla la ruta del archivo de respaldo del resto de pruebas.
        qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
        CredentialStore::setInstance(CredentialStore::createFileStore());
        CredentialStore* store = CredentialStore::instance();

        QString error;
        QVERIFY2(store->write(QStringLiteral("equipo|3389|user|"), QStringLiteral("secreto"),
                              &error),
                 qPrintable(error));

        const QString path = store->filePath();
        QVERIFY(!path.isEmpty());
        QVERIFY2(QFile::exists(path), qPrintable(path));
        QVERIFY(path.startsWith(home.path()));
        QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions());
        QVERIFY(QFile::permissions(path) & QFile::ReadOwner);

        store->remove(QStringLiteral("equipo|3389|user|"));
        CredentialStore::setInstance(nullptr);
    }

    void failedConnectionReportsErrorAndStops()
    {
        RdpSession session;
        QSignalSpy stateSpy(&session, &SessionBackend::stateChanged);
        QSignalSpy errorSpy(&session, &SessionBackend::error);

        ConnectionProfile profile;
        profile.host = QStringLiteral("127.0.0.1");
        profile.port = 1; // puerto cerrado: falla la conexión de inmediato

        SessionBackend::Credentials credentials;
        credentials.username = QStringLiteral("prueba");

        QVERIFY(session.start(profile, credentials));
        QCOMPARE(session.state(), SessionState::Connecting);

        QVERIFY(errorSpy.wait(20000));
        QCOMPARE(errorSpy.count(), 1);
        QCOMPARE(session.state(), SessionState::Error);

        const SessionError failure = errorSpy.at(0).at(0).value<SessionError>();
        QVERIFY(!failure.title.isEmpty());
        QVERIFY(!failure.message.isEmpty());

        QElapsedTimer timer;
        timer.start();
        session.requestStop();
        QVERIFY2(timer.elapsed() < 5000, "requestStop() tardó demasiado");
        QCOMPARE(session.state(), SessionState::Disconnected);
    }

    void stopIsIdempotentAndSafeWithoutStart()
    {
        RdpSession session;
        session.requestStop();
        session.requestStop();
        QCOMPARE(session.state(), SessionState::Disconnected);
    }

    void startRejectsInvalidProfile()
    {
        RdpSession session;
        QSignalSpy errorSpy(&session, &SessionBackend::error);
        ConnectionProfile profile;

        QVERIFY(!session.start(profile, SessionBackend::Credentials()));
        QCOMPARE(errorSpy.count(), 1);
        QCOMPARE(session.state(), SessionState::Disconnected);
    }

    void ctrlAltDeleteIsReservedCombination()
    {
        RdpSession session;
        KeyCombination combination;
        combination.key = Qt::Key_Delete;
        combination.modifiers = Qt::ControlModifier | Qt::AltModifier;
        QVERIFY(session.handlesKeyCombination(combination));

        combination.key = Qt::Key_C;
        QVERIFY(!session.handlesKeyCombination(combination));
    }

    void invalidProfileIsRejected()
    {
        ConnectionProfile profile;
        QVERIFY(!profile.isValid());
        profile.host = QStringLiteral("   ");
        QVERIFY(!profile.isValid());
        profile.host = QStringLiteral("equipo");
        QVERIFY(profile.isValid());
    }
};

QTEST_MAIN(TestCore)

#include "TestCore.moc"
