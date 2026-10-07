#include "ui/SessionWindow.h"

#include "ui/SessionView.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QStyle>
#include <QStyleFactory>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace qtrdp {

SessionWindow::SessionWindow(SessionBackend* backend, const ConnectionProfile& profile,
                             QWidget* parent)
    : QMainWindow(parent), m_backend(backend), m_profile(profile)
{
    buildUi();
    connectBackend();
    applyTheme();
    applyDesktopSize();
}

SessionWindow::~SessionWindow() = default;

void SessionWindow::buildUi()
{
    setWindowTitle(QStringLiteral("%1 - Conexión de Escritorio remoto")
                       .arg(m_profile.displayName().isEmpty() ? m_profile.host
                                                             : m_profile.displayName()));

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_view = new SessionView(m_backend.data(), central);
    layout->addWidget(m_view, 1);

    m_statusLabel = new QLabel(central);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setVisible(false);
    layout->addWidget(m_statusLabel, 0);

    setCentralWidget(central);

    auto* toolbar = new QToolBar(this);
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolbar->setIconSize(QSize(16, 16));

    m_clipboardButton = new QToolButton(toolbar);
    m_clipboardButton->setText(QStringLiteral("Portapapeles"));
    m_clipboardButton->setToolTip(
	    QStringLiteral("El portapapeles compartido se implementará en una fase posterior"));
    m_clipboardButton->setEnabled(false);
    connect(m_clipboardButton, &QToolButton::clicked, this, [this]()
    {
        if (!m_backend)
            return;
        const QClipboard* clipboard = QGuiApplication::clipboard();
        m_backend->sendClipboardText(clipboard->text());
        showStatusMessage(QStringLiteral("Envío de portapapeles no implementado todavía"), true);
    });
    toolbar->addWidget(m_clipboardButton);

    toolbar->addSeparator();

    auto* sasButton = new QToolButton(toolbar);
    sasButton->setText(QStringLiteral("Ctrl+Alt+Del"));
    sasButton->setToolTip(QStringLiteral("Enviar la secuencia de seguridad Ctrl+Alt+Del"));
    connect(sasButton, &QToolButton::clicked, this, [this]()
    {
        if (m_backend)
            m_backend->sendCtrlAltDel();
    });
    toolbar->addWidget(sasButton);

    auto* fullScreenButton = new QToolButton(toolbar);
    fullScreenButton->setText(QStringLiteral("Pantalla completa"));
    fullScreenButton->setCheckable(true);
    connect(fullScreenButton, &QToolButton::clicked, this, [this]()
    {
        if (isFullScreen())
            showNormal();
        else
            showFullScreen();
    });
    toolbar->addWidget(fullScreenButton);

    auto* disconnectButton = new QToolButton(toolbar);
    disconnectButton->setText(QStringLiteral("Desconectar"));
    connect(disconnectButton, &QToolButton::clicked, this, [this]()
    {
        close();
    });
    toolbar->addWidget(disconnectButton);

    addToolBar(Qt::TopToolBarArea, toolbar);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this]()
    {
        if (m_statusLabel)
            m_statusLabel->setVisible(false);
    });

    connect(m_view, &SessionView::escapePressed, this, [this]()
    {
        showNormal();
    });
}

void SessionWindow::connectBackend()
{
    if (!m_backend)
        return;

    connect(m_backend, &SessionBackend::frameReady, this, [this](const QImage& frame)
    {
        m_view->setFrame(frame);
    });

    connect(m_backend, &SessionBackend::frameSizeChanged, this, [this](const QSize& size)
    {
        m_view->setDesktopSize(size);
    });

    connect(m_backend, &SessionBackend::stateChanged, this, [this](SessionState state)
    {
        const bool active = state == SessionState::Connected;
        if (m_clipboardButton)
        {
            // La vista sólo pide resolución dinámica si el backend la soporta.
            m_view->setDynamicResolution(m_backend->capabilities().contains(
                SessionCapability::DynamicResolution));
            m_clipboardButton->setEnabled(active && m_backend->capabilities().contains(
                                                                SessionCapability::Clipboard));
        }

        switch (state)
        {
            case SessionState::Connecting:
                showStatusMessage(QStringLiteral("Conectando con %1...").arg(m_profile.host));
                break;
            case SessionState::Authenticating:
                showStatusMessage(QStringLiteral("Autenticando..."));
                break;
            case SessionState::Connected:
                if (m_statusTimer)
                    m_statusTimer->start(1500);
                m_view->setFocus();
                break;
            case SessionState::Error:
            case SessionState::Disconnected:
                break;
            default:
                break;
        }
    });

    connect(m_backend, &SessionBackend::error, this, [this](const SessionError& error)
    {
        showStatusMessage(QStringLiteral("%1: %2").arg(error.title, error.message));
    });

    connect(m_backend, &SessionBackend::clipboardReceived, this, [this](const QString& text)
    {
        QClipboard* clipboard = QGuiApplication::clipboard();
        if (clipboard && clipboard->text() != text)
            clipboard->setText(text, QClipboard::Clipboard);
    });

    connect(m_backend, &SessionBackend::disconnected, this, [this](const QString&)
    {
        if (isFullScreen())
            showNormal();
    });
}

void SessionWindow::applyTheme()
{
    if (!style()->objectName().isEmpty() && style()->objectName() == QStringLiteral("fusion"))
        return;

    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        setStyle(fusion);

    setStyleSheet(QStringLiteral(R"(
QMainWindow {
    background: #1f1f1f;
}

QToolBar {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 rgba(236, 244, 252, 235), stop:1 rgba(214, 228, 242, 225));
    border: none;
    border-bottom: 1px solid rgba(120, 150, 180, 160);
    spacing: 4px;
    padding: 2px;
}

QToolButton {
    background: rgba(255, 255, 255, 0);
    border: 1px solid rgba(255, 255, 255, 0);
    border-radius: 3px;
    padding: 4px 10px;
    color: #17324d;
}

QToolButton:hover {
    background: rgba(255, 255, 255, 130);
    border: 1px solid rgba(120, 160, 200, 120);
}

QToolButton:pressed {
    background: rgba(150, 185, 220, 150);
}

QToolButton:disabled {
    color: rgba(23, 50, 77, 90);
}

QLabel {
    color: #e8eef5;
    padding: 6px 10px;
    background: rgba(30, 45, 60, 200);
}
)"));
}

void SessionWindow::applyDesktopSize()
{
    const QSize size = m_backend ? m_backend->desktopSize() : QSize();
    if (size.isValid() && !size.isEmpty())
    {
        m_view->setDesktopSize(size);

        const QSize available = QGuiApplication::primaryScreen()->availableGeometry().size();
        QSize target(size);
        if ((target.width() > available.width()) || (target.height() > available.height()))
            target.scale(available.width(), available.height(), Qt::KeepAspectRatio);

        resize(target + QSize(0, toolBarAreaHeight()));
    }
}

int SessionWindow::toolBarAreaHeight() const
{
    for (QToolBar* bar : findChildren<QToolBar*>())
    {
        if (!bar->isVisible())
            continue;
        return bar->sizeHint().height();
    }
    return 0;
}

void SessionWindow::showStatusMessage(const QString& text, bool temporary){
    if (!m_statusLabel)
        return;

    m_statusLabel->setText(text);
    m_statusLabel->setVisible(true);

    if (temporary && m_statusTimer)
        m_statusTimer->start(3000);
    else if (m_statusTimer)
        m_statusTimer->stop();
}

void SessionWindow::closeEvent(QCloseEvent* event)
{
    if (m_backend)
        m_backend->requestStop();
    event->accept();
    Q_EMIT finished();
}

void SessionWindow::keyPressEvent(QKeyEvent* event)
{
    KeyCombination combination;
    combination.key = static_cast<Qt::Key>(event->key());
    combination.modifiers = event->modifiers();

    if (m_backend && m_backend->handlesKeyCombination(combination))
    {
        m_backend->handleKeyCombination(combination);
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

} // namespace qtrdp
