#include "core/GnomeFullscreen.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QGuiApplication>
#include <QTimer>
#include <QVariantMap>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

#include <cstdio>
#include <cstring>

namespace qtrdp {
namespace {

bool debugEnabled()
{
    return qEnvironmentVariableIsSet("QTRDP_DEBUG_FULLSCREEN");
}

void fsDebug(const QString& message)
{
    if (debugEnabled())
        std::fprintf(stderr, "[qtrdp-fs] %s\n", qPrintable(message));
}

constexpr const char* kPortalService = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* kShortcutsInterface = "org.freedesktop.portal.GlobalShortcuts";
constexpr const char* kRequestInterface = "org.freedesktop.portal.Request";
constexpr const char* kShortcutId = "toggle-fullscreen";

Display* ensureDisplay()
{
    static Display* display = XOpenDisplay(nullptr);
    return display;
}

bool windowIsFullscreen(Display* display, Window window, Atom wmState, Atom fullscreen)
{
    Atom type = 0;
    int format = 0;
    unsigned long nitems = 0;
    unsigned long bytesAfter = 0;
    unsigned char* data = nullptr;
    bool fullscreenState = false;

    if (XGetWindowProperty(display, window, wmState, 0, 1024, False, XA_ATOM, &type, &format,
                           &nitems, &bytesAfter, &data) == Success &&
        (data != nullptr))
    {
        const Atom* atoms = reinterpret_cast<const Atom*>(data);
        for (unsigned long i = 0; i < nitems; ++i)
        {
            if (atoms[i] == fullscreen)
            {
                fullscreenState = true;
                break;
            }
        }
        XFree(data);
    }
    return fullscreenState;
}

Window findWindowByPid(Display* display, Window root, unsigned long pid, Atom pidAtom, int depth)
{
    if (depth > 12)
        return 0;

    Window rootReturn = 0;
    Window parentReturn = 0;
    Window* children = nullptr;
    unsigned int childCount = 0;
    if (XQueryTree(display, root, &rootReturn, &parentReturn, &children, &childCount) == 0)
        return 0;

    Window found = 0;
    for (unsigned int i = 0; (i < childCount) && (found == 0); ++i)
    {
        const Window child = children[i];

        Atom type = 0;
        int format = 0;
        unsigned long nitems = 0;
        unsigned long bytesAfter = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(display, child, pidAtom, 0, 1, False, XA_CARDINAL, &type, &format,
                               &nitems, &bytesAfter, &data) == Success &&
            (data != nullptr))
        {
            const unsigned long windowPid = *reinterpret_cast<unsigned long*>(data);
            XFree(data);
            if ((nitems >= 1) && (windowPid == pid))
                found = child;
        }

        if (found == 0)
            found = findWindowByPid(display, child, pid, pidAtom, depth + 1);
    }

    if (children != nullptr)
        XFree(children);
    return found;
}

QString requestPathForToken(const QString& token)
{
    QString sender = QDBusConnection::sessionBus().baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

} // namespace

GnomeFullscreen::GnomeFullscreen(QObject* parent) : QObject(parent) {}

GnomeFullscreen::~GnomeFullscreen() = default;

bool GnomeFullscreen::shouldActivate()
{
    const QByteArray desktop = qgetenv("XDG_CURRENT_DESKTOP").toLower();
    const QByteArray sessionType = qgetenv("XDG_SESSION_TYPE").toLower();
    const bool gnome = desktop.contains("gnome");
    const bool wayland = (sessionType == "wayland") ||
                         (QGuiApplication::platformName().contains(QLatin1String("wayland")));
    return gnome && wayland;
}

void GnomeFullscreen::setTargetPid(qint64 pid)
{
    m_targetPid = pid;
}

void GnomeFullscreen::setStartFullscreen(bool start)
{
    m_startFullscreen = start;
}

void GnomeFullscreen::registerShortcut()
{
    m_createToken = QStringLiteral("qtrdp_create");
    m_bindToken = QStringLiteral("qtrdp_bind");

    const QString path = requestPathForToken(m_createToken);
    fsDebug(QStringLiteral("CreateSession; baseService=%1 ruta=%2")
                .arg(QDBusConnection::sessionBus().baseService(), path));
    QDBusConnection::sessionBus().connect(
        QString::fromLatin1(kPortalService), path, QString::fromLatin1(kRequestInterface),
        QStringLiteral("Response"), this, SLOT(onSessionCreated(uint, QVariantMap)));

    QDBusMessage message =
        QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                       QString::fromLatin1(kPortalPath),
                                       QString::fromLatin1(kShortcutsInterface),
                                       QStringLiteral("CreateSession"));
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), m_createToken);
    options.insert(QStringLiteral("session_handle_token"), QStringLiteral("qtrdp_session"));
    message << options;
    QDBusConnection::sessionBus().asyncCall(message);
}

void GnomeFullscreen::onSessionCreated(uint response, const QVariantMap& results)
{
    fsDebug(QStringLiteral("CreateSession Response=%1").arg(response));
    if (response != 0)
    {
        Q_EMIT shortcutFailed(QStringLiteral("El portal rechazó la sesión (código %1).")
                                  .arg(response));
        return;
    }

    m_sessionHandle = results.value(QStringLiteral("session_handle")).toString();
    if (m_sessionHandle.isEmpty())
    {
        Q_EMIT shortcutFailed(QStringLiteral("El portal no devolvió un identificador de sesión."));
        return;
    }

    // Escucha de activaciones (una sola vez por sesión).
    QDBusConnection::sessionBus().connect(
        QString::fromLatin1(kPortalService), QString::fromLatin1(kPortalPath),
        QString::fromLatin1(kShortcutsInterface), QStringLiteral("Activated"), this,
        SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));

    bindShortcuts();
}

void GnomeFullscreen::bindShortcuts()
{
    const QString path = requestPathForToken(m_bindToken);
    fsDebug(QStringLiteral("BindShortcuts; ruta=%1").arg(path));
    QDBusConnection::sessionBus().connect(
        QString::fromLatin1(kPortalService), path, QString::fromLatin1(kRequestInterface),
        QStringLiteral("Response"), this, SLOT(onShortcutsBound(uint, QVariantMap)));

    QDBusArgument shortcuts;
    QVariantMap shortcutProperties;
    shortcutProperties.insert(
        QStringLiteral("description"),
        QStringLiteral("Alternar pantalla completa de la sesión remota"));
    shortcutProperties.insert(QStringLiteral("preferred_trigger"),
                              QStringLiteral("CTRL+ALT+Return"));
    shortcuts.beginArray();
    shortcuts.beginStructure();
    shortcuts << QString::fromLatin1(kShortcutId);
    shortcuts << shortcutProperties;
    shortcuts.endStructure();
    shortcuts.endArray();

    QDBusMessage message =
        QDBusMessage::createMethodCall(QString::fromLatin1(kPortalService),
                                       QString::fromLatin1(kPortalPath),
                                       QString::fromLatin1(kShortcutsInterface),
                                       QStringLiteral("BindShortcuts"));
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), m_bindToken);
    message << QVariant::fromValue(QDBusObjectPath(m_sessionHandle))
            << QVariant::fromValue(shortcuts) << QString() << options;
    QDBusConnection::sessionBus().asyncCall(message);
}

void GnomeFullscreen::onShortcutsBound(uint response, const QVariantMap& results)
{
    fsDebug(QStringLiteral("BindShortcuts Response=%1").arg(response));
    if (response != 0)
    {
        Q_EMIT shortcutFailed(QStringLiteral("No se pudo registrar el atajo (código %1).")
                                  .arg(response));
        return;
    }

    QString description = QStringLiteral("Ctrl+Alt+Enter");
    bool bound = false;
    const QVariant shortcutsVariant = results.value(QStringLiteral("shortcuts"));
    if (shortcutsVariant.canConvert<QDBusArgument>())
    {
        QDBusArgument argument = shortcutsVariant.value<QDBusArgument>();
        argument.beginArray();
        while (!argument.atEnd())
        {
            argument.beginStructure();
            QString id;
            QVariantMap properties;
            argument >> id;
            argument >> properties;
            argument.endStructure();
            if (id == QString::fromLatin1(kShortcutId))
            {
                bound = true;
                const QString trigger =
                    properties.value(QStringLiteral("trigger_description")).toString();
                if (!trigger.isEmpty())
                    description = trigger;
            }
        }
        argument.endArray();
    }

    if (!bound)
    {
        Q_EMIT shortcutFailed(
            QStringLiteral("El atajo de pantalla completa no quedó registrado."));
        return;
    }

    m_shortcutReady = true;
    Q_EMIT shortcutReady(description);
    applyStartFullscreenIfReady();
}

void GnomeFullscreen::onActivated(const QDBusObjectPath&, const QString& shortcutId, qulonglong,
                                  const QVariantMap&)
{
    fsDebug(QStringLiteral("Activated: %1").arg(shortcutId));
    if (shortcutId == QString::fromLatin1(kShortcutId))
        toggleFullscreen();
}

void GnomeFullscreen::applyStartFullscreenIfReady()
{
    if (!m_startFullscreen || !m_shortcutReady || (m_targetPid <= 0))
        return;

    // La ventana remota puede tardar en aparecer; reintenta hasta 5 s.
    auto* timer = new QTimer(this);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this,
            [this, timer]()
            {
                Display* display = ensureDisplay();
                if (display == nullptr)
                {
                    timer->stop();
                    timer->deleteLater();
                    return;
                }
                const Atom pidAtom = XInternAtom(display, "_NET_WM_PID", True);
                const Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);
                const Atom fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
                const Window window =
                    findWindowByPid(display, DefaultRootWindow(display),
                                    static_cast<unsigned long>(m_targetPid), pidAtom, 0);
                if (window == 0)
                    return;

                if (!windowIsFullscreen(display, window, wmState, fullscreen))
                {
                    XEvent event;
                    std::memset(&event, 0, sizeof(event));
                    event.xclient.type = ClientMessage;
                    event.xclient.window = window;
                    event.xclient.message_type = wmState;
                    event.xclient.format = 32;
                    event.xclient.data.l[0] = 1; // _NET_WM_STATE_ADD
                    event.xclient.data.l[1] = static_cast<long>(fullscreen);
                    event.xclient.data.l[3] = 1; // origen: aplicación normal
                    XSendEvent(display, DefaultRootWindow(display), False,
                               SubstructureRedirectMask | SubstructureNotifyMask, &event);
                    XFlush(display);
                }

                timer->stop();
                timer->deleteLater();
            });
    timer->start();
}

void GnomeFullscreen::toggleFullscreen()
{
    if (m_targetPid <= 0)
        return;

    Display* display = ensureDisplay();
    if (display == nullptr)
        return;

    const Atom pidAtom = XInternAtom(display, "_NET_WM_PID", True);
    const Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);
    const Atom fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
    const Window window = findWindowByPid(display, DefaultRootWindow(display),
                                          static_cast<unsigned long>(m_targetPid), pidAtom, 0);
    fsDebug(QStringLiteral("Toggle fullscreen pid=%1 ventana=%2")
                .arg(m_targetPid)
                .arg(static_cast<qulonglong>(window)));
    if (window == 0)
        return;

    const bool isFullscreen = windowIsFullscreen(display, window, wmState, fullscreen);

    XEvent event;
    std::memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = wmState;
    event.xclient.format = 32;
    event.xclient.data.l[0] = isFullscreen ? 0 : 1; // REMOVE / ADD
    event.xclient.data.l[1] = static_cast<long>(fullscreen);
    event.xclient.data.l[3] = 1;
    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
}

} // namespace qtrdp
