#pragma once

#include <QDBusObjectPath>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace qtrdp {

// Controla la pantalla completa de la ventana de xfreerdp en GNOME/Wayland.
//
// En este entorno el toggle interno de FreeRDP (<Ctrl>+<Alt>+<Enter> y el
// botón de la floatbar) no es fiable por una interacción Mutter/XWayland: al
// restaurar, Mutter interpreta el resize como una nueva petición de fullscreen
// y la ventana queda atrapada (FreeRDP #10993 / #11737). Por eso aquí se
// alterna _NET_WM_STATE_FULLSCREEN directamente vía EWMH sobre la ventana de
// xfreerdp (identificada por _NET_WM_PID) y se expone un atajo global
// registrado con el portal org.freedesktop.portal.GlobalShortcuts.
class GnomeFullscreen final : public QObject
{
    Q_OBJECT

public:
    explicit GnomeFullscreen(QObject* parent = nullptr);
    ~GnomeFullscreen() override;

    // Sólo tiene sentido en GNOME sobre Wayland; en Niri u otros WMs el toggle
    // nativo de FreeRDP funciona y no interferimos.
    static bool shouldActivate();

    // PID del proceso xfreerdp cuya ventana controlamos.
    void setTargetPid(qint64 pid);

    // Si el perfil pide abrir en pantalla completa, se aplica en cuanto la
    // ventana remota aparece y el atajo queda registrado.
    void setStartFullscreen(bool start);

    // Registra el atajo global (asíncrono). Emite shortcutReady/shortcutFailed.
    void registerShortcut();

signals:
    void shortcutReady(const QString& triggerDescription);
    void shortcutFailed(const QString& reason);

private slots:
    void onSessionCreated(uint response, const QVariantMap& results);
    void onShortcutsBound(uint response, const QVariantMap& results);
    void onActivated(const QDBusObjectPath& sessionHandle, const QString& shortcutId,
                     qulonglong timestamp, const QVariantMap& options);

public slots:
    // Alterna la pantalla completa de la ventana objetivo.
    void toggleFullscreen();

private:
    void bindShortcuts();
    void applyStartFullscreenIfReady();

    qint64 m_targetPid = 0;
    bool m_startFullscreen = false;
    bool m_shortcutReady = false;
    QString m_sessionHandle;
    QString m_createToken;
    QString m_bindToken;
};

} // namespace qtrdp
