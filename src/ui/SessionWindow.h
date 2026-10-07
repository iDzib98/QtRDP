#pragma once

#include "core/ConnectionProfile.h"
#include "core/SessionBackend.h"

#include <QMainWindow>
#include <QPointer>

QT_BEGIN_NAMESPACE
class QLabel;
class QTimer;
class QToolButton;
QT_END_NAMESPACE

namespace qtrdp {

class SessionView;

class SessionWindow : public QMainWindow
{
    Q_OBJECT

public:
    SessionWindow(SessionBackend* backend, const ConnectionProfile& profile,
                  QWidget* parent = nullptr);
    ~SessionWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildUi();
    void connectBackend();
    void applyTheme();
    void applyDesktopSize();
    void showStatusMessage(const QString& text, bool temporary = false);
    int toolBarAreaHeight() const;

    QPointer<SessionBackend> m_backend;
    ConnectionProfile m_profile;
    SessionView* m_view = nullptr;
    QLabel* m_statusLabel = nullptr;
    QToolButton* m_clipboardButton = nullptr;
    QTimer* m_statusTimer = nullptr;
    QByteArray m_clipboardBuffer;

signals:
    void finished();
};

} // namespace qtrdp
