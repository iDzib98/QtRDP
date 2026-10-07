#pragma once

#include "core/SessionBackend.h"

#include <memory>

namespace qtrdp {

struct RdpContextData;
class RdpRuntime;

class RdpSession final : public SessionBackend
{
    Q_OBJECT

public:
    explicit RdpSession(QObject* parent = nullptr);
    ~RdpSession() override;

    QString protocol() const override;
    QString displayName() const override;

    bool start(const ConnectionProfile& profile, const Credentials& credentials) override;
    void requestStop() override;

    void sendKeyEvent(const KeyEvent& event) override;
    void sendPointerEvent(const PointerEvent& event) override;
    void sendText(const QString& text) override;
    void sendClipboardText(const QString& text) override;
    void sendCtrlAltDel() override;
    void requestDesktopSize(const QSize& size) override;

    QSize desktopSize() const override;
    SessionState state() const override;
    QString userName() const override;
    QString domain() const override;
    QString hostName() const override;
    QList<SessionCapability> capabilities() const override;

    bool handlesKeyCombination(const KeyCombination& combination) const override;
    void handleKeyCombination(const KeyCombination& combination) override;

private:
    std::unique_ptr<RdpRuntime> d;
};

} // namespace qtrdp
