#pragma once

#include "core/ConnectionProfile.h"
#include "core/SessionTypes.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QStringList>

namespace qtrdp {

class SessionBackend : public QObject
{
    Q_OBJECT

public:
    struct Credentials
    {
        QString username;
        QString password;
        QString domain;
    };

    using QObject::QObject;

    virtual QString protocol() const = 0;
    virtual QString displayName() const = 0;

    virtual bool start(const ConnectionProfile& profile, const Credentials& credentials) = 0;
    virtual void requestStop() = 0;

    virtual void sendKeyEvent(const KeyEvent& event) = 0;
    virtual void sendPointerEvent(const PointerEvent& event) = 0;
    virtual void sendText(const QString& text) = 0;
    virtual void sendClipboardText(const QString& text) = 0;
    virtual void sendCtrlAltDel() = 0;
    virtual void requestDesktopSize(const QSize& size) = 0;

    virtual QSize desktopSize() const = 0;
    virtual SessionState state() const = 0;
    virtual QString userName() const = 0;
    virtual QString domain() const = 0;
    virtual QString hostName() const = 0;
    virtual QList<SessionCapability> capabilities() const = 0;

    virtual bool handlesKeyCombination(const KeyCombination& combination) const
    {
        Q_UNUSED(combination);
        return false;
    }
    virtual void handleKeyCombination(const KeyCombination& combination) = 0;

Q_SIGNALS:
    void stateChanged(qtrdp::SessionState state);
    void connected();
    void disconnected(const QString& reason);
    void frameReady(const QImage& frame);
    void frameSizeChanged(const QSize& size);
    void error(const qtrdp::SessionError& error);
    void capabilitiesChanged(const QList<qtrdp::SessionCapability>& capabilities);
    void clipboardReceived(const QString& text);
    void logMessage(const QString& message, int level);
};

} // namespace qtrdp
