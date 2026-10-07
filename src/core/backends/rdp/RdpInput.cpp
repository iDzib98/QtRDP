#include "core/backends/rdp/RdpInput.h"

#include <freerdp/client.h>
#include <freerdp/freerdp.h>
#include <freerdp/input.h>
#include <freerdp/scancode.h>

#include <QHash>
#include <QString>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace qtrdp {
namespace {

struct ExtendedKey
{
    int qtKey;
    xkb_keysym_t keysym;
    quint16 scancode;
};

const ExtendedKey extendedKeys[] = {
    { Qt::Key_Insert, XKB_KEY_Insert, RDP_SCANCODE_INSERT },
    { Qt::Key_Delete, XKB_KEY_Delete, RDP_SCANCODE_DELETE },
    { Qt::Key_Home, XKB_KEY_Home, RDP_SCANCODE_HOME },
    { Qt::Key_End, XKB_KEY_End, RDP_SCANCODE_END },
    { Qt::Key_Left, XKB_KEY_Left, RDP_SCANCODE_LEFT },
    { Qt::Key_Up, XKB_KEY_Up, RDP_SCANCODE_UP },
    { Qt::Key_Right, XKB_KEY_Right, RDP_SCANCODE_RIGHT },
    { Qt::Key_Down, XKB_KEY_Down, RDP_SCANCODE_DOWN },
    { Qt::Key_PageUp, XKB_KEY_Page_Up, RDP_SCANCODE_PRIOR },
    { Qt::Key_PageDown, XKB_KEY_Page_Down, RDP_SCANCODE_NEXT },
    { Qt::Key_NumLock, XKB_KEY_Num_Lock, RDP_SCANCODE_NUMLOCK },
    { Qt::Key_Print, XKB_KEY_Print, RDP_SCANCODE_PRINTSCREEN },
    { Qt::Key_Alt, XKB_KEY_Alt_L, MAKE_RDP_SCANCODE(0x38, TRUE) },
    { Qt::Key_AltGr, XKB_KEY_ISO_Level3_Shift, MAKE_RDP_SCANCODE(0x38, TRUE) },
    { Qt::Key_Meta, XKB_KEY_Super_L, MAKE_RDP_SCANCODE(0x5B, TRUE) },
    { Qt::Key_Menu, XKB_KEY_Menu, MAKE_RDP_SCANCODE(0x5D, TRUE) },
};

struct KeypadKey
{
    int qtKey;
    quint16 scancode;
};

const KeypadKey keypadKeys[] = {
    { Qt::Key_Enter, RDP_SCANCODE_RETURN_KP }, { Qt::Key_Slash, RDP_SCANCODE_DIVIDE },
    { Qt::Key_Asterisk, RDP_SCANCODE_MULTIPLY }, { Qt::Key_Minus, RDP_SCANCODE_SUBTRACT },
    { Qt::Key_Plus, RDP_SCANCODE_ADD }, { Qt::Key_Period, RDP_SCANCODE_DECIMAL },
    { Qt::Key_Home, RDP_SCANCODE_HOME }, { Qt::Key_End, RDP_SCANCODE_END },
    { Qt::Key_Up, RDP_SCANCODE_UP }, { Qt::Key_Down, RDP_SCANCODE_DOWN },
    { Qt::Key_Left, RDP_SCANCODE_LEFT }, { Qt::Key_Right, RDP_SCANCODE_RIGHT },
    { Qt::Key_PageUp, RDP_SCANCODE_PRIOR }, { Qt::Key_PageDown, RDP_SCANCODE_NEXT },
    { Qt::Key_Insert, RDP_SCANCODE_INSERT }, { Qt::Key_Delete, RDP_SCANCODE_DELETE },
};

const QHash<int, xkb_keysym_t>& keysymOverrides()
{
    static const QHash<int, xkb_keysym_t> map = {
        { Qt::Key_Backspace, XKB_KEY_BackSpace },
        { Qt::Key_Tab, XKB_KEY_Tab },
        { Qt::Key_Backtab, XKB_KEY_ISO_Left_Tab },
        { Qt::Key_Return, XKB_KEY_Return },
        { Qt::Key_Escape, XKB_KEY_Escape },
        { Qt::Key_Shift, XKB_KEY_Shift_L },
        { Qt::Key_Control, XKB_KEY_Control_L },
        { Qt::Key_AltGr, XKB_KEY_ISO_Level3_Shift },
        { Qt::Key_CapsLock, XKB_KEY_Caps_Lock },
        { Qt::Key_ScrollLock, XKB_KEY_Scroll_Lock },
        { Qt::Key_Pause, XKB_KEY_Pause },
        { Qt::Key_Space, XKB_KEY_space },
        { Qt::Key_F1, XKB_KEY_F1 },
        { Qt::Key_F2, XKB_KEY_F2 },
        { Qt::Key_F3, XKB_KEY_F3 },
        { Qt::Key_F4, XKB_KEY_F4 },
        { Qt::Key_F5, XKB_KEY_F5 },
        { Qt::Key_F6, XKB_KEY_F6 },
        { Qt::Key_F7, XKB_KEY_F7 },
        { Qt::Key_F8, XKB_KEY_F8 },
        { Qt::Key_F9, XKB_KEY_F9 },
        { Qt::Key_F10, XKB_KEY_F10 },
        { Qt::Key_F11, XKB_KEY_F11 },
        { Qt::Key_F12, XKB_KEY_F12 },
        { Qt::Key_F13, XKB_KEY_F13 },
        { Qt::Key_F14, XKB_KEY_F14 },
        { Qt::Key_F15, XKB_KEY_F15 },
        { Qt::Key_F16, XKB_KEY_F16 },
        { Qt::Key_F17, XKB_KEY_F17 },
        { Qt::Key_F18, XKB_KEY_F18 },
        { Qt::Key_F19, XKB_KEY_F19 },
        { Qt::Key_F20, XKB_KEY_F20 },
        { Qt::Key_F21, XKB_KEY_F21 },
        { Qt::Key_F22, XKB_KEY_F22 },
        { Qt::Key_F23, XKB_KEY_F23 },
        { Qt::Key_F24, XKB_KEY_F24 },
    };
    return map;
}

bool isExtended(Qt::Key key)
{
    for (const auto& entry : extendedKeys)
    {
        if (entry.qtKey == key)
            return true;
    }
    return false;
}

quint16 extendedScancode(Qt::Key key)
{
    for (const auto& entry : extendedKeys)
    {
        if (entry.qtKey == key)
            return entry.scancode;
    }
    return 0;
}

quint16 keypadScancode(Qt::Key key)
{
    for (const auto& entry : keypadKeys)
    {
        if (entry.qtKey == key)
            return entry.scancode;
    }
    return 0;
}

} // namespace

RdpInput::RdpInput(rdpClientContext* context) : m_context(context)
{
    m_xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (m_xkbContext)
        m_keymap = xkb_keymap_new_from_names(m_xkbContext, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
}

RdpInput::~RdpInput()
{
    if (m_keymap)
        xkb_keymap_unref(m_keymap);
    if (m_xkbContext)
        xkb_context_unref(m_xkbContext);
}

quint16 RdpInput::scancodeForKey(Qt::Key key) const
{
    const int rawKey = static_cast<int>(key);

    if (isExtended(key))
        return extendedScancode(key);

    xkb_keysym_t keysym = 0;
    const auto overrideIt = keysymOverrides().constFind(rawKey);
    if (overrideIt != keysymOverrides().cend())
        keysym = overrideIt.value();
    else if ((rawKey > 0) && (rawKey < 0x01000000))
        keysym = static_cast<xkb_keysym_t>(rawKey);

    if (keysym == 0 || !m_keymap)
        return 0;

    const xkb_keycode_t keycode = keycodeForKeysym(keysym);
    if (keycode == 0)
        return 0;

    return static_cast<quint16>(keycode);
}

xkb_keycode_t RdpInput::keycodeForKeysym(xkb_keysym_t keysym) const
{
    if (!m_keymap)
        return 0;

    const auto cached = m_keymapCache.constFind(keysym);
    if (cached != m_keymapCache.cend())
        return cached.value();

    xkb_keymap* keymap = m_keymap;
    const xkb_keycode_t first = xkb_keymap_min_keycode(keymap);
    const xkb_keycode_t last = xkb_keymap_max_keycode(keymap);
    xkb_keycode_t found = 0;

    for (xkb_keycode_t keycode = first; keycode <= last; ++keycode)
    {
        const xkb_keysym_t* syms = nullptr;
        const int count = xkb_keymap_key_get_syms_by_level(keymap, keycode, 0, 0, &syms);
        if (count <= 0 || !syms)
            continue;

        for (int index = 0; index < count; ++index)
        {
            if (syms[index] == keysym)
            {
                found = keycode;
                break;
            }
        }

        if (found != 0)
            break;
    }

    auto* self = const_cast<RdpInput*>(this);
    self->m_keymapCache.insert(keysym, found);
    return found;
}

bool RdpInput::sendScancode(bool down, bool repeat, quint16 scancode)
{
    if (!m_context || !m_context->context.input || scancode == 0)
        return false;
    return freerdp_input_send_keyboard_event_ex(m_context->context.input, down ? TRUE : FALSE,
	                                            repeat ? TRUE : FALSE, scancode) == TRUE;
}

bool RdpInput::sendUnicode(bool down, const QString& text)
{
    if (!m_context || !m_context->context.input || text.isEmpty())
        return false;
    const UINT16 flags = down ? 0 : KBD_FLAGS_RELEASE;
    const QChar first = text.at(0);
    const bool ok =
	    freerdp_input_send_unicode_keyboard_event(m_context->context.input, flags,
	                                              static_cast<UINT16>(first.unicode())) == TRUE;
    if (ok && down)
    {
        freerdp_input_send_unicode_keyboard_event(m_context->context.input, KBD_FLAGS_RELEASE,
	                                             static_cast<UINT16>(first.unicode()));
    }
    return ok;
}

bool RdpInput::sendKey(const KeyEvent& event)
{
    const bool down = event.action == KeyAction::Press;

    if (event.key == Qt::Key_Pause)
    {
        if (!m_context || !m_context->context.input)
            return false;
        if (down && !event.autoRepeat)
            return freerdp_input_send_keyboard_pause_event(m_context->context.input) == TRUE;
        return true;
    }

    if (event.modifiers.testFlag(Qt::KeypadModifier))
    {
        const quint16 keypad = keypadScancode(event.key);
        if (keypad != 0)
            return sendScancode(down, event.autoRepeat, keypad);
    }

    const quint16 scancode = scancodeForKey(event.key);
    if (scancode != 0)
        return sendScancode(down, event.autoRepeat, scancode);

    if (!event.text.isEmpty())
        return sendUnicode(down, event.text);

    return false;
}

bool RdpInput::sendText(const QString& text)
{
    bool ok = true;
    for (const QChar character : text)
    {
        const QString chunk(character);
        ok = sendUnicode(true, chunk) && ok;
    }
    return ok;
}

bool RdpInput::sendPointer(const PointerEvent& event)
{
    if (!m_context)
        return false;

    const BOOL relative = freerdp_client_use_relative_mouse_events(m_context) ? TRUE : FALSE;
    const INT32 x = relative ? event.x : static_cast<INT32>(event.x);
    const INT32 y = relative ? event.y : static_cast<INT32>(event.y);

    switch (event.action)
    {
        case PointerAction::Move:
            return freerdp_client_send_button_event(m_context, relative, PTR_FLAGS_MOVE, x, y) == TRUE;

        case PointerAction::ButtonDown:
        case PointerAction::ButtonUp:
        {
            UINT16 flags = 0;
            if (event.button == Qt::LeftButton)
                flags = PTR_FLAGS_BUTTON1;
            else if (event.button == Qt::RightButton)
                flags = PTR_FLAGS_BUTTON2;
            else if (event.button == Qt::MiddleButton)
                flags = PTR_FLAGS_BUTTON3;
            else if (event.button == Qt::ExtraButton4)
                flags = PTR_FLAGS_BUTTON1 | PTR_FLAGS_BUTTON2;
            else if (event.button == Qt::ExtraButton5)
                flags = PTR_FLAGS_BUTTON1 | PTR_FLAGS_BUTTON3;
            else
                return false;

            if (event.action == PointerAction::ButtonDown)
                flags |= PTR_FLAGS_DOWN;
            return freerdp_client_send_button_event(m_context, relative, flags, x, y) == TRUE;
        }

        case PointerAction::Wheel:
        case PointerAction::WheelHorizontal:
        {
            int steps = event.wheelDelta;
            if (event.wheelAngleDelta != 0)
                steps = event.wheelAngleDelta / 120;
            if (steps == 0)
                return false;

            const int sign = steps < 0 ? -1 : 1;
            steps = qAbs(steps);
            steps = qBound(1, steps, 8);

            UINT16 flags = 0;
            if (event.action == PointerAction::WheelHorizontal)
                flags |= PTR_FLAGS_HWHEEL;
            else
                flags |= PTR_FLAGS_WHEEL;

            for (int i = 0; i < steps; ++i)
            {
                UINT16 stepFlags = flags;
                if (sign < 0)
                    stepFlags |= PTR_FLAGS_WHEEL_NEGATIVE | (0x100 - 0x78);
                else
                    stepFlags |= 0x78;
                if (!freerdp_client_send_wheel_event(m_context, stepFlags))
                    return false;
            }
            return true;
        }

        case PointerAction::TouchDown:
            return freerdp_client_handle_touch(m_context, FREERDP_TOUCH_DOWN, 0, 0, x, y) == TRUE;
        case PointerAction::TouchMove:
            return freerdp_client_handle_touch(m_context, FREERDP_TOUCH_MOTION, 0, 0, x, y) == TRUE;
        case PointerAction::TouchUp:
            return freerdp_client_handle_touch(m_context, FREERDP_TOUCH_UP, 0, 0, x, y) == TRUE;

        default:
            return false;
    }
}

bool RdpInput::sendCtrlAltDel()
{
    bool ok = sendScancode(true, false, RDP_SCANCODE_LCONTROL);
    ok = sendScancode(true, false, RDP_SCANCODE_LMENU) && ok;
    ok = sendScancode(true, false, RDP_SCANCODE_DELETE) && ok;
    ok = sendScancode(false, false, RDP_SCANCODE_DELETE) && ok;
    ok = sendScancode(false, false, RDP_SCANCODE_LMENU) && ok;
    ok = sendScancode(false, false, RDP_SCANCODE_LCONTROL) && ok;
    return ok;
}

bool RdpInput::sendFocus(bool)
{
    if (!m_context || !m_context->context.input)
        return false;
    return freerdp_input_send_focus_in_event(m_context->context.input, 0) == TRUE;
}

} // namespace qtrdp
