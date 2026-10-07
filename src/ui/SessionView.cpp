#include "ui/SessionView.h"

#include <QColor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

namespace qtrdp {

SessionView::SessionView(SessionBackend* backend, QWidget* parent)
    : QWidget(parent), m_backend(backend)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
    setCursor(Qt::BlankCursor);

    if (m_backend)
        m_desktopSize = m_backend->desktopSize();
}

SessionView::~SessionView() = default;

SessionBackend* SessionView::backend() const
{
    return m_backend.data();
}

void SessionView::setFrame(const QImage& frame)
{
    if (frame.isNull())
        return;

    m_frame = frame;

    if (m_frame.size() != m_desktopSize)
        setDesktopSize(m_frame.size());

    // El servidor confirmó el nuevo tamaño: la ventana manda de nuevo.
    if ((!m_lastRequestedSize.isEmpty()) && (m_desktopSize == m_lastRequestedSize))
        m_lastRequestedSize = QSize();

    update();
}

void SessionView::setDesktopSize(const QSize& size)
{
    if (size.isEmpty() || size == m_desktopSize)
        return;

    m_desktopSize = size;
    updateGeometry();
    update();
    Q_EMIT desktopSizeChanged(m_desktopSize);
}

QSize SessionView::desktopSize() const
{
    return m_desktopSize;
}

QPoint SessionView::toDesktop(const QPoint& position) const
{
    if (m_frame.isNull() || m_frame.size().isEmpty())
        return position;

    const QSize target = m_frame.size();
    const qreal scaleX = static_cast<qreal>(target.width()) / qMax(1, width());
    const qreal scaleY = static_cast<qreal>(target.height()) / qMax(1, height());

    return QPoint(qBound(0, qRound(position.x() * scaleX), target.width() - 1),
                  qBound(0, qRound(position.y() * scaleY), target.height() - 1));
}

void SessionView::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), QColor(Qt::black));

    if (m_frame.isNull())
        return;

    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_frame.size() != size());
    painter.drawImage(rect(), m_frame);
}

void SessionView::forwardKey(QKeyEvent* event)
{
    if (!m_backend)
        return;

    KeyEvent key;
    key.action = (event->type() == QEvent::KeyRelease) ? KeyAction::Release : KeyAction::Press;
    key.key = static_cast<Qt::Key>(event->key());
    key.text = event->text();
    key.modifiers = event->modifiers();
    key.autoRepeat = event->isAutoRepeat();

    m_backend->sendKeyEvent(key);
}

void SessionView::keyPressEvent(QKeyEvent* event)
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

    if ((event->key() == Qt::Key_Escape) && (event->modifiers() == Qt::NoModifier))
        Q_EMIT escapePressed();

    forwardKey(event);
    event->accept();
}

void SessionView::keyReleaseEvent(QKeyEvent* event)
{
    KeyCombination combination;
    combination.key = static_cast<Qt::Key>(event->key());
    combination.modifiers = event->modifiers();

    if (m_backend && m_backend->handlesKeyCombination(combination))
    {
        event->accept();
        return;
    }

    forwardKey(event);
    event->accept();
}

void SessionView::forwardMouse(PointerAction action, const QPoint& position,
                               Qt::MouseButton button)
{
    if (!m_backend)
        return;

    PointerEvent event;
    event.action = action;
    event.x = position.x();
    event.y = position.y();
    event.button = button;
    m_backend->sendPointerEvent(event);
}

void SessionView::mousePressEvent(QMouseEvent* event)
{
    setFocus(Qt::MouseFocusReason);
    forwardMouse(PointerAction::ButtonDown, toDesktop(event->position().toPoint()),
                 event->button());
    event->accept();
}

void SessionView::mouseReleaseEvent(QMouseEvent* event)
{
    forwardMouse(PointerAction::ButtonUp, toDesktop(event->position().toPoint()), event->button());
    event->accept();
}

void SessionView::mouseMoveEvent(QMouseEvent* event)
{
    forwardMouse(PointerAction::Move, toDesktop(event->position().toPoint()), Qt::NoButton);
    event->accept();
}

void SessionView::wheelEvent(QWheelEvent* event)
{
    if (!m_backend)
        return;

    PointerEvent pointer;
    const QPoint position = toDesktop(event->position().toPoint());
    pointer.x = position.x();
    pointer.y = position.y();
    pointer.wheelDelta = event->angleDelta().y();
    pointer.wheelAngleDelta = event->angleDelta().y();
    pointer.action =
	    event->modifiers() & Qt::ShiftModifier ? PointerAction::WheelHorizontal
	                                           : PointerAction::Wheel;

    m_backend->sendPointerEvent(pointer);
    event->accept();
}

void SessionView::enterEvent(QEnterEvent* event)
{
    forwardMouse(PointerAction::Enter, toDesktop(event->position().toPoint()), Qt::NoButton);
    QWidget::enterEvent(event);
}

void SessionView::leaveEvent(QEvent* event)
{
    forwardMouse(PointerAction::Leave, QPoint(-1, -1), Qt::NoButton);
    QWidget::leaveEvent(event);
}

void SessionView::focusInEvent(QFocusEvent* event)
{
    m_focused = true;
    QWidget::focusInEvent(event);
}

void SessionView::focusOutEvent(QFocusEvent* event)
{
    m_focused = false;
    QWidget::focusOutEvent(event);
}

void SessionView::setDynamicResolution(bool enabled)
{
    m_dynamicResolution = enabled;
}

bool SessionView::dynamicResolution() const
{
    return m_dynamicResolution;
}

void SessionView::scheduleDynamicResize()
{
    if (!m_dynamicResolution || !m_backend)
        return;

    // El tamaño de la ventana es el del área útil, sin el marco ni la barra.
    const QSize viewport = size();
    if (viewport.isEmpty())
        return;

    if (viewport == m_desktopSize)
        return;

    // Mientras el servidor no confirma, se compara contra la última petición.
    const QSize reference = m_lastRequestedSize.isEmpty() ? m_desktopSize : m_lastRequestedSize;
    const int delta = qAbs(viewport.width() - reference.width()) +
		      qAbs(viewport.height() - reference.height());
    if (delta < 16)
        return;

    m_pendingSize = viewport;

    if (!m_resizeTimer)
    {
        m_resizeTimer = new QTimer(this);
        m_resizeTimer->setSingleShot(true);
        m_resizeTimer->setInterval(300);
        connect(m_resizeTimer, &QTimer::timeout, this, &SessionView::requestDynamicResize);
    }

    m_resizeTimer->start();
}

void SessionView::requestDynamicResize()
{
    if (!m_dynamicResolution || !m_backend)
        return;

    if (m_pendingSize.isEmpty() || (m_pendingSize == m_desktopSize))
        return;

    if (m_pendingSize == m_lastRequestedSize)
        return;

    m_lastRequestedSize = m_pendingSize;
    m_backend->requestDesktopSize(m_pendingSize);
}

void SessionView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    scheduleDynamicResize();
}

} // namespace qtrdp
