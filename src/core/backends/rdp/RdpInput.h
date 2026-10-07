#pragma once

#include "core/SessionTypes.h"

#include <freerdp/freerdp.h>

#include <QHash>

struct xkb_context;
struct xkb_keymap;

namespace qtrdp {

class RdpInput
{
public:
    explicit RdpInput(rdpClientContext* context);
    ~RdpInput();

    RdpInput(const RdpInput&) = delete;
    RdpInput& operator=(const RdpInput&) = delete;

    bool sendKey(const KeyEvent& event);
    bool sendText(const QString& text);
    bool sendPointer(const PointerEvent& event);
    bool sendCtrlAltDel();
    bool sendFocus(bool focused);

private:
    bool sendScancode(bool down, bool repeat, quint16 scancode);
    bool sendUnicode(bool down, const QString& text);
    quint16 scancodeForKey(Qt::Key key) const;
    unsigned int keycodeForKeysym(unsigned int keysym) const;

    rdpClientContext* m_context = nullptr;
    xkb_context* m_xkbContext = nullptr;
    xkb_keymap* m_keymap = nullptr;
    mutable QHash<unsigned int, unsigned int> m_keymapCache;
};

} // namespace qtrdp
