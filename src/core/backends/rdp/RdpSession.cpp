#include "core/CredentialFormat.h"
#include "core/backends/rdp/RdpSession.h"

#include "core/backends/rdp/RdpInput.h"

#include <freerdp/channels/disp.h>
#include <freerdp/client.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/client/disp.h>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/codec/color.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/gdi/gfx.h>
#include <freerdp/settings.h>

#include <winpr/crt.h>
#include <winpr/error.h>
#include <winpr/synch.h>
#include <winpr/thread.h>
#include <winpr/wtypes.h>

#include <QAtomicInt>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QString>

#include <cstring>

namespace qtrdp {

namespace {

constexpr DWORD kMaxWaitHandles = 16;
constexpr int kCommandQueueLimit = 8192;
// Margen para que un servidor aplique un Monitor Layout antes de avisar.
constexpr int kResizeConfirmTimeoutMs = 3000;
constexpr int kResizeRetryIntervalMs = 600;
constexpr int kResizeMaxRetries = 4;

enum class CommandType
{
    Key,
    Pointer,
    Text,
    Clipboard,
    CtrlAltDel,
    Resize,
};

struct Command
{
    CommandType type = CommandType::Key;
    KeyEvent key;
    PointerEvent pointer;
    QString text;
    QSize size;
};

} // namespace

// Extensión del contexto que FreeRDP reserva con calloc: solo tipos triviales.
// El estado con objetos C++ vive en RdpRuntime, enlazado mediante runtime.
struct RdpContextData
{
    rdpClientContext common{};
    RdpRuntime* runtime = nullptr;
};

namespace {
UINT qtrdp_end_frame(RdpgfxClientContext* gfx, const RDPGFX_END_FRAME_PDU* endFrame);
BOOL qtrdp_bitmap_update(rdpContext* context, const BITMAP_UPDATE* bitmap);
} // namespace

class RdpRuntime
{
public:
    explicit RdpRuntime(RdpSession* session) : owner(session)
    {
    }

    RdpSession* owner = nullptr;
    rdpContext* context = nullptr;
    RdpInput* input = nullptr;
    RdpgfxClientContext* gfx = nullptr;
    DispClientContext* disp = nullptr;

    pcRdpgfxEndFrame previousEndFrame = nullptr;
    pBitmapUpdate previousBitmapUpdate = nullptr;
    bool gfxHooked = false;

    QAtomicInt dynamicResolutionReady{0}; // El servidor aceptó el canal DISP.
    QAtomicInt resizeRequests{0};        // Peticiones de Monitor Layout enviadas.
    QAtomicInt resizeWarnings{0};        // Avisos ya mostrados al usuario.
    bool resizePending = false;          // Hay un cambio de tamaño sin confirmar.
    QSize resizeTarget;
    QMutex resizeMutex;
    QSize requestedDesktopSize;          // Último tamaño pedido por la interfaz.
    QElapsedTimer resizePendingSince;
    int resizeRetryCount = 0;
    UINT32 desktopScaleFactor = 100;
    ConnectionProfile profile;           // Perfil con el que se inició la sesión.

    HANDLE wakeup = nullptr;
    HANDLE thread = nullptr;

    QAtomicInt state{static_cast<int>(SessionState::Disconnected)};
    QAtomicInt running{0};
    QAtomicInt stopRequested{0};
    QAtomicInt contextAlive{0};

    mutable QMutex mutex;
    QQueue<Command> commands;

    QMutex sizeMutex;
    QSize desktopSize;

    void setState(SessionState newState)
    {
        const int previous = state.fetchAndStoreRelaxed(static_cast<int>(newState));
        if (previous == static_cast<int>(newState))
            return;

        RdpSession* session = owner;
        QMetaObject::invokeMethod(
            session,
            [session, newState]()
            {
                Q_EMIT session->stateChanged(newState);
                if (newState == SessionState::Connected)
                    Q_EMIT session->connected();
            },
            Qt::QueuedConnection);
    }

    void postError(SessionErrorKind kind, const QString& title, const QString& message,
                   const QString& details = QString(), bool closesSession = true)
    {
        SessionError error;
        error.kind = kind;
        error.title = title;
        error.message = message;
        error.details = details;
        error.closesSession = closesSession;

        RdpSession* session = owner;
        QMetaObject::invokeMethod(
            session,
            [session, error]()
            {
                Q_EMIT session->error(error);
            },
            Qt::QueuedConnection);
    }

    void log(const QString& message, int level = 1)
    {
        RdpSession* session = owner;
        QMetaObject::invokeMethod(
            session,
            [session, message, level]()
            {
                Q_EMIT session->logMessage(message, level);
            },
            Qt::QueuedConnection);
    }

    // Idempotente: el canal RDPGFX puede conectarse antes o después de PostConnect.
    void setupGraphics()
    {
        if (!gfx || !context || !context->gdi)
            return;

        rdpGdi* gdi = context->gdi;

        if (!gdi->gfx)
        {
            gdi_graphics_pipeline_init(gdi, gfx);
        }

        if (!previousEndFrame && gfx->EndFrame)
        {
            gfx->custom = gdi;
            previousEndFrame = gfx->EndFrame;
            gfx->EndFrame = qtrdp_end_frame;
            gfxHooked = true;
            log(QStringLiteral("Pipeline de vídeo RDPEGFX conectado (%1x%2)")
                     .arg(gdi->width)
                     .arg(gdi->height),
                 0);
        }
    }

    // Los servidores que no usan RDPEGFX envían BitmapUpdate sobre el mismo
    // framebuffer, así que también hay que publicar fotogramas en ese camino.
    void hookBitmapUpdates()
    {
        if (!context || !context->update || previousBitmapUpdate)
            return;

        previousBitmapUpdate = context->update->BitmapUpdate;
        context->update->BitmapUpdate = qtrdp_bitmap_update;
        log(QStringLiteral("Recepción de imágenes por BitmapUpdate"), 0);
    }

    void announceDesktopSize(const QSize& size, bool force)
    {
        if (size.isEmpty())
            return;

        bool resized = false;
        {
            QMutexLocker locker(&sizeMutex);
            if (desktopSize != size)
            {
                desktopSize = size;
                resized = true;
            }
        }

        if (!resized && !force)
            return;

        RdpSession* session = owner;
        QMetaObject::invokeMethod(
            session,
            [session, size]()
            {
                Q_EMIT session->frameSizeChanged(size);
            },
            Qt::QueuedConnection);
    }

    void publishFrame()
    {
        rdpGdi* gdi = context ? context->gdi : nullptr;
        if (!gdi || !gdi->primary_buffer || (gdi->width == 0) || (gdi->height == 0))
            return;

        const int width = gdi->width;
        const int height = gdi->height;
        const int stride = gdi->stride;

        // ¿El servidor aplicó el cambio de tamaño solicitado?
        if (resizePending)
        {
            if ((width == resizeTarget.width()) && (height == resizeTarget.height()))
            {
                resizePending = false;
            }
            else if (resizePendingSince.isValid() &&
                     (resizeRetryCount < kResizeMaxRetries) &&
                     (resizePendingSince.elapsed() >=
                      (resizeRetryCount + 1) * kResizeRetryIntervalMs))
            {
                // Windows puede completar la apertura del canal después del
                // primer PDU; xfreerdp3 reintenta al recibir nuevos frames.
                ++resizeRetryCount;
                applyDesktopSize(resizeTarget);
            }
            else if (resizePendingSince.isValid() &&
                     (resizePendingSince.elapsed() > kResizeConfirmTimeoutMs) &&
                     !resizeWarnings.loadRelaxed())
            {
                resizePending = false;
                if (resizeWarnings.fetchAndStoreRelaxed(1) == 0)
                {
                    postError(SessionErrorKind::Unsupported,
                              QStringLiteral("Resolución dinámica no disponible"),
                              QStringLiteral(
                                  "El servidor conectado no cambia el tamaño del escritorio "
                                  "remoto al redimensionar la ventana. La imagen se "
                                  "escalará para ajustarse."),
                              QStringLiteral("Monitor Layout PDU sin efecto"), false);
                }
            }
        }

        announceDesktopSize(QSize(width, height), false);

        const QImage view(static_cast<const uchar*>(gdi->primary_buffer), width, height, stride,
                          QImage::Format_RGB32);
        if (view.isNull())
            return;

        const QImage frame = view.copy();
        RdpSession* session = owner;
        QMetaObject::invokeMethod(
            session,
            [session, frame]()
            {
                Q_EMIT session->frameReady(frame);
            },
            Qt::QueuedConnection);
    }

    void enqueue(const Command& command)
    {
        if (!running.loadRelaxed())
            return;

        {
            QMutexLocker locker(&mutex);
            if (commands.size() >= kCommandQueueLimit)
                return;
            commands.enqueue(command);
        }

        if (wakeup)
            SetEvent(wakeup);
    }

    void drainCommands()
    {
        QQueue<Command> pending;
        {
            QMutexLocker locker(&mutex);
            pending.swap(commands);
        }

        for (const Command& command : pending)
        {
            if (!input)
                continue;

            switch (command.type)
            {
                case CommandType::Key:
                    input->sendKey(command.key);
                    break;
                case CommandType::Pointer:
                    input->sendPointer(command.pointer);
                    break;
                case CommandType::Text:
                    input->sendText(command.text);
                    break;
                case CommandType::CtrlAltDel:
                    input->sendCtrlAltDel();
                    break;
                case CommandType::Resize:
                    applyDesktopSize(command.size);
                    break;
                case CommandType::Clipboard:
                    break;
            }
        }
    }

    void applyDesktopSize(const QSize& requested)
    {
        if (!requested.isValid() || requested.isEmpty())
            return;

        if (!disp || !disp->SendMonitorLayout || !dynamicResolutionReady.loadRelaxed())
            return;

        rdpGdi* gdi = context ? context->gdi : nullptr;
        if (!gdi || (gdi->width == 0) || (gdi->height == 0))
            return;

        // El protocolo limita el tamaño de cada monitor a 200..8192 píxeles.
        const int width =
	    qBound(DISPLAY_CONTROL_MIN_MONITOR_WIDTH, requested.width(),
		   static_cast<int>(DISPLAY_CONTROL_MAX_MONITOR_WIDTH));
        const int height =
	    qBound(DISPLAY_CONTROL_MIN_MONITOR_HEIGHT, requested.height(),
		   static_cast<int>(DISPLAY_CONTROL_MAX_MONITOR_HEIGHT));

        // Nada que hacer si ya tiene ese tamaño (evita bucles con el cliente).
        if ((gdi->width == width) && (gdi->height == height))
            return;

        DISPLAY_CONTROL_MONITOR_LAYOUT layout = {};
        layout.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
        layout.Width = static_cast<UINT32>(width);
        layout.Height = static_cast<UINT32>(height);
        // Tamaño físico de la pantalla en milímetros (típico de un monitor).
        // Igual que el cliente X11 de FreeRDP: 75 DPI expresados en mm.
        layout.PhysicalWidth = static_cast<UINT32>(qRound(width / 75.0 * 25.4));
        layout.PhysicalHeight = static_cast<UINT32>(qRound(height / 75.0 * 25.4));
        layout.DesktopScaleFactor =
            freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopScaleFactor);
        layout.DeviceScaleFactor =
            freerdp_settings_get_uint32(context->settings, FreeRDP_DeviceScaleFactor);
        layout.Orientation =
            freerdp_settings_get_uint16(context->settings, FreeRDP_DesktopOrientation);

        // Windows espera que estos valores acompañen al Monitor Layout PDU;
        // xfreerdp3 los actualiza cada vez que cambia el tamaño de su ventana.
        freerdp_settings_set_uint32(context->settings, FreeRDP_SmartSizingWidth,
                                    static_cast<UINT32>(width));
        freerdp_settings_set_uint32(context->settings, FreeRDP_SmartSizingHeight,
                                    static_cast<UINT32>(height));

        const UINT status = disp->SendMonitorLayout(disp, 1, &layout);
        const int attempt = resizeRequests.fetchAndAddRelaxed(1) + 1;

        if (status != CHANNEL_RC_OK)
        {
            log(QStringLiteral("El servidor rechazó el cambio de tamaño (%1×%2)")
                    .arg(width).arg(height), 2);
            return;
        }

        if (attempt <= 4)
            log(QStringLiteral("Solicitando escritorio de %1×%2").arg(width).arg(height), 1);

        // Se vigila que el servidor confirme el tamaño; publishFrame se encarga
        // de avisar una sola vez si nunca llega a aplicarse.
        const QSize target(width, height);
        if (!resizePending || (resizeTarget != target))
        {
            resizeTarget = target;
            resizePending = true;
            resizeRetryCount = 0;
            resizePendingSince.start();
        }
    }
};

namespace {

RdpRuntime* runtimeFromContext(void* context)
{
    if (!context)
        return nullptr;
    return static_cast<RdpContextData*>(context)->runtime;
}

UINT qtrdp_end_frame(RdpgfxClientContext* gfx, const RDPGFX_END_FRAME_PDU* endFrame)
{
    if (!gfx || !gfx->custom)
        return CHANNEL_RC_OK;

    auto* gdi = static_cast<rdpGdi*>(gfx->custom);
    if (!gdi->context)
        return CHANNEL_RC_OK;

    RdpRuntime* data = runtimeFromContext(gdi->context);
    if (!data)
        return CHANNEL_RC_OK;

    UINT status = CHANNEL_RC_OK;
    if (data->previousEndFrame)
        status = data->previousEndFrame(gfx, endFrame);

    if (status == CHANNEL_RC_OK)
        data->publishFrame();

    return status;
}

BOOL qtrdp_bitmap_update(rdpContext* context, const BITMAP_UPDATE* bitmap)
{
    RdpRuntime* data = runtimeFromContext(context);
    BOOL status = data && data->previousBitmapUpdate
	                      ? data->previousBitmapUpdate(context, bitmap)
	                      : TRUE;

    if (data && (status == TRUE))
        data->publishFrame();

    return status;
}

// El cliente anuncia su capacidad de resolución dinámica; el servidor responde
// y a partir de ahí se pueden enviar PDUs de Monitor Layout.
UINT qtrdp_disp_caps(DispClientContext* context, UINT32 maxNumMonitors,
                     UINT32 maxMonitorAreaFactorA, UINT32 maxMonitorAreaFactorB)
{
    if (!context || !context->custom)
        return CHANNEL_RC_OK;

    auto* data = static_cast<RdpRuntime*>(context->custom);
    data->dynamicResolutionReady.storeRelaxed(1);
    data->log(QStringLiteral("Resolución dinámica negociada (hasta %1 monitores)").arg(maxNumMonitors),
	      1);

    // La ventana puede haberse redimensionado antes de recibir CAPS. En ese
    // caso la petición quedó retenida y se envía ahora, como hace xfreerdp3.
    QSize requested;
    {
        QMutexLocker locker(&data->resizeMutex);
        requested = data->requestedDesktopSize;
    }
    if (requested.isValid() && !requested.isEmpty())
        data->applyDesktopSize(requested);

    return CHANNEL_RC_OK;
}

void qtrdp_on_channel_connected(void* context, const ChannelConnectedEventArgs* e)
{
    if (!context || !e || !e->name)
        return;

    RdpRuntime* data = runtimeFromContext(context);

    const bool isGraphics = std::strcmp(e->name, RDPGFX_DVC_CHANNEL_NAME) == 0;
    freerdp_client_OnChannelConnectedEventHandler(context, e);

    if (!data)
        return;

    if (isGraphics)
    {
        data->gfx = static_cast<RdpgfxClientContext*>(e->pInterface);
        data->setupGraphics();
    }
    else if (std::strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0)
    {
        data->disp = static_cast<DispClientContext*>(e->pInterface);
        if (data->disp)
        {
            data->disp->custom = data;
            // Sin este callback el cliente nunca envía el PDU de capacidades y
            // el servidor ignora los cambios de tamaño.
            data->disp->DisplayControlCaps = qtrdp_disp_caps;
        }
        data->log(QStringLiteral("Canal de resolución dinámica (disp) conectado"), 0);
    }
}

void qtrdp_on_channel_disconnected(void* context, const ChannelDisconnectedEventArgs* e)
{
    if (!context || !e || !e->name)
        return;

    RdpRuntime* data = runtimeFromContext(context);

    freerdp_client_OnChannelDisconnectedEventHandler(context, e);

    if (!data)
        return;

    if (std::strcmp(e->name, RDPGFX_DVC_CHANNEL_NAME) == 0)
    {
        data->gfx = nullptr;
        data->previousEndFrame = nullptr;
    }
    else if (std::strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0)
    {
        data->disp = nullptr;
        data->dynamicResolutionReady.storeRelaxed(0);
    }
}

BOOL qtrdp_post_connect(freerdp* instance)
{
    rdpContext* context = instance ? static_cast<rdpContext*>(instance->context) : nullptr;
    if (!context)
        return FALSE;

    RdpRuntime* data = runtimeFromContext(context);
    if (!data)
        return TRUE;

    // El framebuffer y las cachés de FreeRDP solo existen tras la conexión.
    if (!gdi_init(instance, PIXEL_FORMAT_BGRX32))
        return FALSE;

    data->hookBitmapUpdates();
    data->setupGraphics();

    rdpGdi* gdi = context->gdi;
    if (gdi && (gdi->width > 0) && (gdi->height > 0))
        data->announceDesktopSize(QSize(gdi->width, gdi->height), true);

    data->log(QStringLiteral("Sesión activa con %1")
                  .arg(QString::fromUtf8(freerdp_settings_get_string(context->settings, FreeRDP_ServerHostname))),
              0);

    data->setState(SessionState::Connected);
    return TRUE;
}

void qtrdp_post_disconnect(freerdp* instance)
{
    rdpContext* context = instance ? static_cast<rdpContext*>(instance->context) : nullptr;
    if (!context)
        return;

    RdpRuntime* data = runtimeFromContext(context);
    if (!data)
        return;

    data->setState(SessionState::Disconnected);
}

DWORD qtrdp_verify_certificate(freerdp* instance, const char* host, UINT16 port,
                               const char* commonName, const char* subject, const char* issuer,
                               const char* fingerprint, DWORD flags)
{
    (void)commonName;
    (void)subject;
    (void)issuer;
    (void)fingerprint;
    (void)flags;

    rdpContext* context = instance ? static_cast<rdpContext*>(instance->context) : nullptr;
    if (!context)
        return 0;

    RdpRuntime* data = runtimeFromContext(context);
    if (data)
    {
        data->log(QStringLiteral("Certificado de %1 aceptado sin validar")
                      .arg(QString::fromUtf8(host ? host : ""))
                      .arg(port),
                  2);
    }
    return 1;
}

BOOL qtrdp_global_init(void)
{
    return TRUE;
}

void qtrdp_global_uninit(void)
{
}

BOOL qtrdp_client_new(freerdp* instance, rdpContext* context)
{
    (void)instance;
    (void)context;
    return TRUE;
}

void qtrdp_client_free(freerdp* instance, rdpContext* context)
{
    (void)instance;
    (void)context;
}

int qtrdp_client_start(rdpContext* context)
{
    (void)context;
    return 0;
}

int qtrdp_client_stop(rdpContext* context)
{
    (void)context;
    return 0;
}

RDP_CLIENT_ENTRY_POINTS qtrdp_entry_points()
{
    RDP_CLIENT_ENTRY_POINTS entryPoints = {};
    entryPoints.Version = RDP_CLIENT_INTERFACE_VERSION;
    entryPoints.Size = sizeof(RDP_CLIENT_ENTRY_POINTS_V1);
    entryPoints.GlobalInit = qtrdp_global_init;
    entryPoints.GlobalUninit = qtrdp_global_uninit;
    entryPoints.ContextSize = sizeof(RdpContextData);
    entryPoints.ClientNew = qtrdp_client_new;
    entryPoints.ClientFree = qtrdp_client_free;
    entryPoints.ClientStart = qtrdp_client_start;
    entryPoints.ClientStop = qtrdp_client_stop;
    return entryPoints;
}

SessionErrorKind errorKindForLastError(UINT32 code)
{
    switch (code)
    {
        case FREERDP_ERROR_CONNECT_FAILED:
        case FREERDP_ERROR_CONNECT_TRANSPORT_FAILED:
        case FREERDP_ERROR_TLS_CONNECT_FAILED:
        case FREERDP_ERROR_DNS_ERROR:
        case FREERDP_ERROR_DNS_NAME_NOT_FOUND:
        case FREERDP_ERROR_CONNECT_CANCELLED:
        case FREERDP_ERROR_CONNECT_ACTIVATION_TIMEOUT:
        case FREERDP_ERROR_LOGON_TIMEOUT:
        case FREERDP_ERROR_SECURITY_NEGO_CONNECT_FAILED:
            return SessionErrorKind::Network;

        case FREERDP_ERROR_AUTHENTICATION_FAILED:
        case FREERDP_ERROR_CONNECT_LOGON_FAILURE:
        case FREERDP_ERROR_CONNECT_WRONG_PASSWORD:
        case FREERDP_ERROR_CONNECT_ACCESS_DENIED:
        case FREERDP_ERROR_CONNECT_ACCOUNT_DISABLED:
        case FREERDP_ERROR_CONNECT_ACCOUNT_LOCKED_OUT:
        case FREERDP_ERROR_CONNECT_ACCOUNT_EXPIRED:
        case FREERDP_ERROR_CONNECT_ACCOUNT_RESTRICTION:
        case FREERDP_ERROR_CONNECT_PASSWORD_EXPIRED:
        case FREERDP_ERROR_CONNECT_PASSWORD_CERTAINLY_EXPIRED:
        case FREERDP_ERROR_CONNECT_PASSWORD_MUST_CHANGE:
        case FREERDP_ERROR_CONNECT_NO_OR_MISSING_CREDENTIALS:
        case FREERDP_ERROR_CONNECT_LOGON_TYPE_NOT_GRANTED:
        case FREERDP_ERROR_INSUFFICIENT_PRIVILEGES:
            return SessionErrorKind::Authentication;

        default:
            return SessionErrorKind::Network;
    }
}

// Explicación en lenguaje claro: el código numérico solo va en el detalle.
QString explainError(UINT32 code)
{
    switch (code)
    {
        case FREERDP_ERROR_DNS_NAME_NOT_FOUND:
            return QStringLiteral(
                "No se encontró ese equipo. Revisa el nombre o la dirección IP.");
        case FREERDP_ERROR_DNS_ERROR:
            return QStringLiteral(
                "No se pudo resolver el nombre del equipo. Revisa tu conexión a "
                "la red o escribe la dirección IP.");
        case FREERDP_ERROR_CONNECT_TRANSPORT_FAILED:
            return QStringLiteral(
                "No se pudo abrir la conexión con ese puerto del equipo. Comprueba "
                "el puerto y que el servicio de Escritorio remoto esté activo.");
        case FREERDP_ERROR_TLS_CONNECT_FAILED:
            return QStringLiteral(
                "Falló la negociación segura (TLS/NLA) con el servidor.");
        case FREERDP_ERROR_CONNECT_FAILED:
            return QStringLiteral(
                "El servidor rechazó la conexión. Puede que Escritorio remoto no "
                "esté habilitado en ese equipo.");
        case FREERDP_ERROR_CONNECT_TARGET_BOOTING:
            return QStringLiteral("El equipo remoto todavía está arrancando. Intenta "
                                  "de nuevo en unos segundos.");
        case FREERDP_ERROR_CONNECT_ACTIVATION_TIMEOUT:
            return QStringLiteral("El equipo remoto tardó demasiado en responder.");
        case FREERDP_ERROR_CONNECT_CANCELLED:
            return QStringLiteral("Se canceló la conexión.");
        case FREERDP_ERROR_AUTHENTICATION_FAILED:
            return QStringLiteral(
                "Falló la autenticación. Escribe el usuario como DOMINIO\\usuario "
                "y revisa la contraseña.");
        case FREERDP_ERROR_CONNECT_LOGON_FAILURE:
            return QStringLiteral(
                "El servidor rechazó el nombre de usuario o la contraseña. Escribe "
                "el usuario como DOMINIO\\usuario (o .\\usuario para una cuenta "
                "local) y vuelve a capturar la contraseña.");
        case FREERDP_ERROR_CONNECT_WRONG_PASSWORD:
            return QStringLiteral("La contraseña no es correcta.");
        case FREERDP_ERROR_CONNECT_NO_OR_MISSING_CREDENTIALS:
            return QStringLiteral("Faltan las credenciales: escribe usuario y contraseña.");
        case FREERDP_ERROR_CONNECT_ACCESS_DENIED:
            return QStringLiteral(
                "La cuenta no tiene permiso para iniciar sesión en ese equipo.");
        case FREERDP_ERROR_INSUFFICIENT_PRIVILEGES:
            return QStringLiteral("La cuenta no tiene privilegios suficientes.");
        case FREERDP_ERROR_CONNECT_ACCOUNT_DISABLED:
            return QStringLiteral("La cuenta está deshabilitada.");
        case FREERDP_ERROR_CONNECT_ACCOUNT_LOCKED_OUT:
            return QStringLiteral(
                "La cuenta está bloqueada por intentos fallidos. Desbloquéala en el "
                "equipo remoto.");
        case FREERDP_ERROR_CONNECT_ACCOUNT_EXPIRED:
            return QStringLiteral("La cuenta ha caducado.");
        case FREERDP_ERROR_CONNECT_PASSWORD_EXPIRED:
        case FREERDP_ERROR_CONNECT_PASSWORD_CERTAINLY_EXPIRED:
            return QStringLiteral("La contraseña caducó: cámbiala en el equipo remoto.");
        case FREERDP_ERROR_CONNECT_PASSWORD_MUST_CHANGE:
            return QStringLiteral("El servidor exige cambiar la contraseña antes de "
                                  "iniciar sesión.");
        case FREERDP_ERROR_CONNECT_CLIENT_REVOKED:
            return QStringLiteral("El servidor revocó esta conexión.");
        case FREERDP_ERROR_CONNECT_KDC_UNREACHABLE:
            return QStringLiteral("No se pudo contactar con el controlador de dominio.");
        case FREERDP_ERROR_CONNECT_HYBRID_REQUIRED_BY_SERVER:
            return QStringLiteral("El servidor exige una cuenta híbrida de Microsoft.");
        default:
            break;
    }

    const UINT32 category = code & 0xFFFF0000u;
    if (category == 0x000C0000u)
        return QStringLiteral("La conexión se cortó. Vuelve a intentarlo.");

    return QStringLiteral("No se pudo iniciar la sesión con ese equipo.");
}

QString titleForErrorKind(SessionErrorKind kind)
{
    switch (kind)
    {
        case SessionErrorKind::Network:
            return QStringLiteral("No se pudo conectar");
        case SessionErrorKind::Authentication:
            return QStringLiteral("Error de credenciales");
        case SessionErrorKind::Certificate:
            return QStringLiteral("Certificado no válido");
        case SessionErrorKind::Gateway:
            return QStringLiteral("Error de puerta de enlace");
        case SessionErrorKind::Unsupported:
            return QStringLiteral("Función no disponible");
        default:
            return QStringLiteral("Error de conexión");
    }
}

DWORD WINAPI qtrdp_session_thread(LPVOID param)
{
    auto* data = static_cast<RdpRuntime*>(param);
    rdpContext* context = data->context;
    if (!context)
        return 1;

    freerdp* instance = freerdp_client_get_instance(context);
    if (!instance)
        return 1;

    data->running.storeRelaxed(1);
    data->setState(SessionState::Connecting);
    data->setState(SessionState::Authenticating);

    if (!freerdp_connect(instance))
    {
        const UINT32 lastError = freerdp_get_last_error(context);
        const SessionErrorKind kind = errorKindForLastError(lastError);
        data->setState(SessionState::Error);
        data->postError(kind, titleForErrorKind(kind), explainError(lastError),
                        QStringLiteral("FreeRDP: %1\nCódigo: 0x%2 (%3)")
                            .arg(QString::fromUtf8(freerdp_get_last_error_string(lastError)),
                                 QString::number(lastError, 16).rightJustified(8, QLatin1Char('0')),
                                 QString::number(lastError)));
        data->running.storeRelaxed(0);
        return 1;
    }

    HANDLE handles[kMaxWaitHandles] = {};

    while (!data->stopRequested.loadRelaxed() && !freerdp_shall_disconnect_context(context))
    {
        DWORD count = 0;
        handles[count++] = data->wakeup;

        const DWORD extra =
            freerdp_get_event_handles(context, &handles[count], kMaxWaitHandles - count);
        if (extra == 0)
            break;
        count += extra;

        const DWORD wait = WaitForMultipleObjects(count, handles, FALSE, INFINITE);
        if (wait == WAIT_FAILED)
            break;

        if ((wait >= WAIT_OBJECT_0) && (wait < WAIT_OBJECT_0 + count) &&
            (handles[wait - WAIT_OBJECT_0] == data->wakeup))
        {
            ResetEvent(data->wakeup);
            data->drainCommands();
        }

        if (!freerdp_check_event_handles(context))
            break;
    }

    data->setState(SessionState::Disconnecting);

    if (freerdp_is_active_state(context))
        freerdp_disconnect(instance);

    data->running.storeRelaxed(0);
    return 0;
}

// Traduce el perfil a la lista de unidades de FreeRDP. Acepta las mismas
// formas que xfreerdp3 /drive:nombre,ruta (y «*» para todas las montadas).
void applyDriveRedirection(rdpContext* context, const ConnectionProfile& profile)
{
    rdpSettings* settings = context->settings;

    const bool wantsHome = profile.redirectHomeDrive;
    const bool wantsDrives = profile.redirectDrives || !profile.drives.isEmpty();
    if (!wantsDrives && !wantsHome)
        return;

    bool added = false;
    QStringList driveNames;

    for (const QString& entry : profile.drives)
    {
        const QString trimmed = entry.trimmed();
        if (trimmed.isEmpty())
            continue;

        QString name;
        QString path = trimmed;
        const int comma = trimmed.indexOf(QLatin1Char(','));
        if (comma > 0)
        {
            name = trimmed.left(comma).trimmed();
            path = trimmed.mid(comma + 1).trimmed();
        }

        if (path.isEmpty())
            continue;

        // Rutas especiales que FreeRDP entiende por su cuenta: «*» (todas las
        // unidades extraíbles), «DynamicDrives» (conexión en caliente) y «%».
        const bool special = (path == QLatin1String("*")) ||
			      (path == QLatin1String("DynamicDrives")) ||
			      (path == QLatin1String("%"));
        const QByteArray nameUtf8 = name.toUtf8();
        const QByteArray pathUtf8 = (path == QLatin1String("%"))
	                                    ? QByteArray(QDir::homePath().toUtf8())
	                                    : path.toUtf8();

        const char* params[3] = { "drive", nullptr, nullptr };
        size_t count = 2;
        params[1] = pathUtf8.constData();
        if (!name.isEmpty() && !special)
        {
            params[1] = nameUtf8.constData();
            params[2] = pathUtf8.constData();
            count = 3;
        }

        if (freerdp_client_add_device_channel(settings, count, params))
        {
            added = true;
            driveNames.append(name.isEmpty() ? path : name);
        }
    }

    if (wantsHome)
    {
        const QByteArray pathUtf8 = QDir::homePath().toUtf8();
        const char* params[3] = { "drive", "home", pathUtf8.constData() };
        if (freerdp_client_add_device_channel(settings, 3, params))
        {
            added = true;
            driveNames.append(QStringLiteral("home"));
        }
    }

    if (added)
    {
        // Los dispositivos compartidos necesitan el canal de redirección.
        freerdp_settings_set_bool(settings, FreeRDP_DeviceRedirection, TRUE);

        if (RdpRuntime* data = runtimeFromContext(context))
        {
            data->log(QStringLiteral("Unidades compartidas: %1")
                          .arg(driveNames.join(QStringLiteral(", "))),
                      0);
        }
    }
}

void qtrdp_configure_settings(rdpContext* context, const ConnectionProfile& profile,
                             const RdpSession::Credentials& credentials)
{
    if (!context || !context->settings)
        return;

    rdpSettings* settings = context->settings;
    freerdp* instance = context->instance;

    const NormalizedCredentials identity = normalizeCredentials(
        credentials.username.isEmpty() ? profile.username : credentials.username,
        credentials.domain.isEmpty() ? profile.domain : credentials.domain);

    const QByteArray hostName = profile.host.toUtf8();
    const QByteArray userName = identity.username.toUtf8();
    const QByteArray domainName = identity.domain.toUtf8();
    const QByteArray password = credentials.password.toUtf8();

    freerdp_settings_set_string(settings, FreeRDP_ServerHostname, hostName.constData());
    freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, profile.port);
    freerdp_settings_set_string(settings, FreeRDP_Username, userName.constData());
    freerdp_settings_set_string(settings, FreeRDP_Domain, domainName.constData());
    freerdp_settings_set_string(settings, FreeRDP_Password, password.constData());

    freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_NegotiateSecurityLayer, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_RedirectClipboard, profile.redirectClipboard);
    freerdp_settings_set_bool(settings, FreeRDP_RedirectPrinters, profile.redirectPrinters);
    freerdp_settings_set_bool(settings, FreeRDP_SupportMonitorLayoutPdu, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_DesktopResize, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_AutoReconnectionEnabled, TRUE);
    freerdp_settings_set_uint32(settings, FreeRDP_AutoReconnectMaxRetries, 3);

    // Resolución dinámica: el escritorio remoto sigue al tamaño de la ventana
    // (equivalente a xfreerdp3 /dynamic-resolution).
    freerdp_settings_set_bool(settings, FreeRDP_SupportDisplayControl, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_DynamicResolutionUpdate,
			      profile.dynamicResolution);

    applyDriveRedirection(context, profile);

    if (!profile.themesEnabled)
        freerdp_settings_set_bool(settings, FreeRDP_DisableThemes, TRUE);
    if (profile.wallpaper.isEmpty())
        freerdp_settings_set_bool(settings, FreeRDP_DisableWallpaper, TRUE);
    if (!profile.fontSmoothing)
        freerdp_settings_set_bool(settings, FreeRDP_AllowFontSmoothing, FALSE);
    if (!profile.desktopComposition)
        freerdp_settings_set_bool(settings, FreeRDP_AllowDesktopComposition, FALSE);

    // RDPEGFX necesita una geometría inicial válida antes de crear sus codecs.
    // La resolución dinámica la sustituirá después de recibir DISP CAPS.
    const UINT32 initialWidth = profile.desktopWidth > 0
                                    ? static_cast<UINT32>(profile.desktopWidth)
                                    : 1024U;
    const UINT32 initialHeight = profile.desktopHeight > 0
                                     ? static_cast<UINT32>(profile.desktopHeight)
                                     : 768U;
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, initialWidth);
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, initialHeight);
    freerdp_settings_set_uint32(settings, FreeRDP_SmartSizingWidth, initialWidth);
    freerdp_settings_set_uint32(settings, FreeRDP_SmartSizingHeight, initialHeight);

    if (profile.scaleFactor > 0)
    {
        freerdp_settings_set_uint32(settings, FreeRDP_DesktopScaleFactor,
                                    static_cast<UINT32>(profile.scaleFactor));
    }

    if (!profile.remoteProgram.isEmpty())
    {
        freerdp_settings_set_bool(settings, FreeRDP_RemoteApplicationMode, TRUE);
        const QByteArray program = profile.remoteProgram.toUtf8();
        freerdp_settings_set_string(settings, FreeRDP_RemoteApplicationProgram,
                                    program.constData());
    }
    else if (!profile.program.isEmpty())
    {
        const QByteArray program = profile.program.toUtf8();
        const QByteArray shell = profile.shell.toUtf8();
        freerdp_settings_set_string(settings, FreeRDP_AlternateShell, program.constData());
        freerdp_settings_set_string(settings, FreeRDP_ShellWorkingDirectory, shell.constData());
    }

    if (profile.gatewayEnabled && !profile.gatewayHostname.isEmpty())
    {
        const QByteArray gateway = profile.gatewayHostname.toUtf8();
        freerdp_settings_set_bool(settings, FreeRDP_GatewayEnabled, TRUE);
        freerdp_settings_set_string(settings, FreeRDP_GatewayHostname, gateway.constData());
        freerdp_settings_set_uint32(settings, FreeRDP_GatewayPort,
                                    profile.gatewayPort > 0
	                                    ? static_cast<UINT32>(profile.gatewayPort)
	                                    : 443U);
        freerdp_settings_set_bool(settings, FreeRDP_GatewayUseSameCredentials,
                                  profile.gatewaySameCredentials);
    }

    (void)instance;
}

} // namespace

RdpSession::RdpSession(QObject* parent)
    : SessionBackend(parent), d(std::make_unique<RdpRuntime>(this))
{
}

RdpSession::~RdpSession()
{
    requestStop();

    if (d->wakeup)
    {
        CloseHandle(d->wakeup);
        d->wakeup = nullptr;
    }
}

QString RdpSession::protocol() const
{
    return QStringLiteral("rdp");
}

QString RdpSession::displayName() const
{
    return QStringLiteral("RDP");
}

bool RdpSession::start(const ConnectionProfile& profile, const Credentials& credentials)
{
    if (d->running.loadRelaxed())
        return false;

    if (!profile.isValid())
    {
        SessionError failure;
        failure.kind = SessionErrorKind::Internal;
        failure.title = QStringLiteral("Perfil inválido");
        failure.message = QStringLiteral("Falta el host o el puerto de destino.");
        Q_EMIT error(failure);
        return false;
    }

    RDP_CLIENT_ENTRY_POINTS entryPoints = qtrdp_entry_points();
    entryPoints.settings = freerdp_settings_new(0);

    rdpContext* context = freerdp_client_context_new(&entryPoints);
    if (!context)
    {
        freerdp_settings_free(entryPoints.settings);
        return false;
    }

    reinterpret_cast<RdpContextData*>(context)->runtime = d.get();
    d->context = context;
    d->contextAlive.storeRelaxed(1);
    d->stopRequested.storeRelaxed(0);
    d->dynamicResolutionReady.storeRelaxed(0);
    d->resizeRequests.storeRelaxed(0);
    d->resizeWarnings.storeRelaxed(0);
    {
        QMutexLocker locker(&d->resizeMutex);
        d->requestedDesktopSize = QSize();
    }
    d->desktopScaleFactor =
	static_cast<UINT32>(qBound(100, profile.scaleFactor, 500));
    d->profile = profile;
    d->state.storeRelaxed(static_cast<int>(SessionState::Connecting));
    d->running.storeRelaxed(1);

    qtrdp_configure_settings(context, profile, credentials);

    auto* cctx = reinterpret_cast<rdpClientContext*>(context);
    freerdp* instance = freerdp_client_get_instance(context);
    if (!instance)
    {
        d->contextAlive.storeRelaxed(0);
        d->context = nullptr;
        freerdp_client_context_free(context);
        return false;
    }

    instance->PostConnect = qtrdp_post_connect;
    instance->PostDisconnect = qtrdp_post_disconnect;
    instance->VerifyCertificateEx = qtrdp_verify_certificate;

    if ((PubSub_SubscribeChannelConnected(context->pubSub, qtrdp_on_channel_connected) < 0) ||
        (PubSub_SubscribeChannelDisconnected(context->pubSub, qtrdp_on_channel_disconnected) < 0))
    {
        d->contextAlive.storeRelaxed(0);
        d->context = nullptr;
        freerdp_client_context_free(context);
        return false;
    }

    d->input = new RdpInput(cctx);
    d->wakeup = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (!d->wakeup)
    {
        delete d->input;
        d->input = nullptr;
        d->contextAlive.storeRelaxed(0);
        d->context = nullptr;
        freerdp_client_context_free(context);
        return false;
    }

    d->thread = CreateThread(nullptr, 0, qtrdp_session_thread, d.get(), 0, nullptr);
    if (!d->thread)
    {
        delete d->input;
        d->input = nullptr;
        CloseHandle(d->wakeup);
        d->wakeup = nullptr;
        d->contextAlive.storeRelaxed(0);
        d->context = nullptr;
        freerdp_client_context_free(context);
        return false;
    }

    return true;
}

void RdpSession::requestStop()
{
    if (d->running.loadRelaxed() || d->thread)
    {
        d->stopRequested.storeRelaxed(1);

        if (d->wakeup)
            SetEvent(d->wakeup);

        if (d->thread)
        {
            WaitForSingleObject(d->thread, 30000);
            CloseHandle(d->thread);
            d->thread = nullptr;
        }

        if (d->contextAlive.loadRelaxed())
        {
            freerdp_client_stop(d->context);
            freerdp_disconnect(freerdp_client_get_instance(d->context));
        }
    }

    if (d->contextAlive.fetchAndStoreRelaxed(0))
    {
        rdpContext* context = d->context;

        if (context && context->pubSub)
        {
            PubSub_UnsubscribeChannelConnected(context->pubSub, qtrdp_on_channel_connected);
            PubSub_UnsubscribeChannelDisconnected(context->pubSub,
                                                  qtrdp_on_channel_disconnected);
        }

        d->context = nullptr;
        freerdp_client_context_free(context);
    }

    delete d->input;
    d->input = nullptr;
    d->gfx = nullptr;
    d->disp = nullptr;
    d->previousEndFrame = nullptr;
    d->setState(SessionState::Disconnected);
}

void RdpSession::sendKeyEvent(const KeyEvent& event)
{
    Command command;
    command.type = CommandType::Key;
    command.key = event;
    d->enqueue(command);
}

void RdpSession::sendPointerEvent(const PointerEvent& event)
{
    Command command;
    command.type = CommandType::Pointer;
    command.pointer = event;
    d->enqueue(command);
}

void RdpSession::sendText(const QString& text)
{
    if (text.isEmpty())
        return;
    Command command;
    command.type = CommandType::Text;
    command.text = text;
    d->enqueue(command);
}

void RdpSession::sendClipboardText(const QString& text)
{
    if (text.isEmpty())
        return;
    Command command;
    command.type = CommandType::Clipboard;
    command.text = text;
    d->enqueue(command);
}

void RdpSession::sendCtrlAltDel()
{
    Command command;
    command.type = CommandType::CtrlAltDel;
    d->enqueue(command);
}

void RdpSession::requestDesktopSize(const QSize& size)
{
    if (size.isEmpty() || !d->running.loadRelaxed())
        return;

    {
        QMutexLocker locker(&d->resizeMutex);
        d->requestedDesktopSize = size;
    }

    Command command;
    command.type = CommandType::Resize;
    command.size = size;
    d->enqueue(command);
}

QSize RdpSession::desktopSize() const
{
    QMutexLocker locker(&d->sizeMutex);
    return d->desktopSize;
}

SessionState RdpSession::state() const
{
    return static_cast<SessionState>(d->state.loadRelaxed());
}

QString RdpSession::userName() const
{
    if (!d->contextAlive.loadRelaxed() || !d->context || !d->context->settings)
        return {};
    const char* name = freerdp_settings_get_string(d->context->settings, FreeRDP_Username);
    return name ? QString::fromUtf8(name) : QString();
}

QString RdpSession::domain() const
{
    if (!d->contextAlive.loadRelaxed() || !d->context || !d->context->settings)
        return {};
    const char* name = freerdp_settings_get_string(d->context->settings, FreeRDP_Domain);
    return name ? QString::fromUtf8(name) : QString();
}

QString RdpSession::hostName() const
{
    if (!d->contextAlive.loadRelaxed() || !d->context || !d->context->settings)
        return {};
    const char* name = freerdp_settings_get_string(d->context->settings, FreeRDP_ServerHostname);
    return name ? QString::fromUtf8(name) : QString();
}

QList<SessionCapability> RdpSession::capabilities() const
{
    const ConnectionProfile& profile = d->profile;
    QList<SessionCapability> result;

    if (profile.dynamicResolution)
        result.append(SessionCapability::DynamicResolution);

    if (profile.redirectDrives || profile.redirectHomeDrive || !profile.drives.isEmpty())
        result.append(SessionCapability::DriveRedirection);

    if (profile.redirectPrinters)
        result.append(SessionCapability::Printing);

    if (profile.redirectAudio)
        result.append(SessionCapability::AudioOutput);

    if (profile.redirectMicrophone)
        result.append(SessionCapability::AudioInput);

    return result;
}

bool RdpSession::handlesKeyCombination(const KeyCombination& combination) const
{
    if (combination.key != Qt::Key_Delete)
        return false;
    return combination.modifiers.testFlag(Qt::ControlModifier) &&
           combination.modifiers.testFlag(Qt::AltModifier);
}

void RdpSession::handleKeyCombination(const KeyCombination& combination)
{
    (void)combination;
    sendCtrlAltDel();
}

} // namespace qtrdp
