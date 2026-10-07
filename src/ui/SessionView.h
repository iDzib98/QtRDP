#pragma once

#include "core/SessionBackend.h"
#include "core/SessionTypes.h"

#include <QImage>
#include <QPointer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace qtrdp {

class SessionView : public QWidget
{
    Q_OBJECT

public:
    explicit SessionView(SessionBackend* backend, QWidget* parent = nullptr);
    ~SessionView() override;

    SessionBackend* backend() const;

    void setFrame(const QImage& frame);
    void setDesktopSize(const QSize& size);
    QSize desktopSize() const;

    // Resolución dinámica: el escritorio remoto sigue al tamaño de la ventana.
    void setDynamicResolution(bool enabled);
    bool dynamicResolution() const;

signals:
    void desktopSizeChanged(const QSize& size);
    void escapePressed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

public:
    QPoint toDesktop(const QPoint& position) const;

private:
    void forwardKey(QKeyEvent* event);
    void forwardMouse(PointerAction action, const QPoint& position, Qt::MouseButton button);
    void scheduleDynamicResize();
    void requestDynamicResize();

    QPointer<SessionBackend> m_backend;
    QImage m_frame;
    QSize m_desktopSize;
    QPoint m_lastMousePosition;
    bool m_focused = false;
    bool m_dynamicResolution = true;
    QTimer* m_resizeTimer = nullptr;
    QSize m_pendingSize;
    QSize m_lastRequestedSize; // Última petición enviada, para no repetirla.
};

} // namespace qtrdp
