#pragma once

#include <QMetaType>
#include <QString>
#include <Qt>

namespace qtrdp {

enum class SessionState
{
    Disconnected,
    Connecting,
    Authenticating,
    Connected,
    Reconnecting,
    Disconnecting,
    Error,
};

enum class SessionErrorKind
{
    None,
    Network,
    Authentication,
    Certificate,
    Gateway,
    Protocol,
    Unsupported,
    Internal,
};

enum class SessionCapability
{
    Clipboard,
    ClipboardFiles,
    AudioOutput,
    AudioInput,
    DriveRedirection,
    Printing,
    DynamicResolution,
    MultiMonitor,
    RemoteApplication,
    SmartCard,
};

enum class PointerAction
{
    Move,
    ButtonDown,
    ButtonUp,
    Wheel,
    WheelHorizontal,
    Enter,
    Leave,
    TouchDown,
    TouchMove,
    TouchUp,
};

enum class KeyAction
{
    Press,
    Release,
};

struct KeyEvent
{
    KeyAction action = KeyAction::Press;
    Qt::Key key = Qt::Key_unknown;
    QString text;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    bool autoRepeat = false;
};

struct PointerEvent
{
    PointerAction action = PointerAction::Move;
    int x = 0;
    int y = 0;
    Qt::MouseButton button = Qt::NoButton;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    int wheelDelta = 0;
    int wheelAngleDelta = 0;
    bool relative = false;
};

struct KeyCombination
{
    Qt::Key key = Qt::Key_unknown;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
};

struct SessionError
{
    SessionErrorKind kind = SessionErrorKind::None;
    QString title;
    QString message;
    QString details;
    int code = 0;
    // Un aviso (por ejemplo, que el servidor no admite resolución dinámica)
    // se muestra pero no termina la sesión.
    bool closesSession = true;
};

} // namespace qtrdp

Q_DECLARE_METATYPE(qtrdp::KeyEvent)
Q_DECLARE_METATYPE(qtrdp::PointerEvent)
Q_DECLARE_METATYPE(qtrdp::SessionError)
Q_DECLARE_METATYPE(qtrdp::KeyCombination)
