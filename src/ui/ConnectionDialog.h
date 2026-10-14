#pragma once

#include "core/ConnectionProfile.h"
#include "core/ProfileStore.h"
#include "core/SessionBackend.h"

#include <QDialog>
#include <QList>
#include <QPair>

QT_BEGIN_NAMESPACE
class QBoxLayout;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QEvent;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLayout;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTabWidget;
class QSpinBox;
class QShowEvent;
class QTimer;
class QToolButton;
class QWidget;
QT_END_NAMESPACE

namespace qtrdp {

class ConnectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ConnectionDialog(QWidget* parent = nullptr);
    ~ConnectionDialog() override;

    qtrdp::ConnectionProfile profile() const;
    qtrdp::SessionBackend::Credentials credentials() const;

    // Solicita credenciales sin obligar a reabrir el gestor de perfiles.
    static bool promptForCredentials(QWidget* parent, ConnectionProfile& profile,
                                     SessionBackend::Credentials& credentials,
                                     bool& rememberPassword,
                                     const QString& reason = QString());

    // Precarga el formulario, por ejemplo al abrir un archivo .rdp.
    void loadProfile(const ConnectionProfile& profile);

private:
    void buildUi();
    QWidget* buildProfileList();
    QWidget* buildForm();
    QWidget* buildScreenPage();
    QWidget* buildLocalResourcesPage();
    QWidget* buildPerformancePage();
    QWidget* buildAdvancedPage();
    void refreshProfileList(const QString& selectId = QString());
    void selectProfile(const QString& id);
    void loadProfileIntoForm(const ConnectionProfile& profile);
    void collectForm();
    void selectLastUsedProfile();
    void updateOkButton();
    void updatePasswordHint();
    [[nodiscard]] QString targetCredentialId() const;
    void updateActionButtons();
    void showError(const QString& message);
    void persistProfile();
    bool persistCredentials(QString* error = nullptr);
    void accept() override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

    void onNewProfile();
    void onDuplicateProfile();
    void onDeleteProfile();
    void onMoveProfile(int offset);
    void onImportRdp();
    void onExportRdp();
    void onProfileNameChanged(const QString& name);
    void setOptionsExpanded(bool expanded);
    void updateQuickUserLabel();
    void updateCommandPreview();
    void saveAsNewProfile();

    // Etiquetas de ayuda del modo desarrollo: muestran el parámetro de xfreerdp
    // que habilita cada campo o casilla cuando el modo desarrollo está activo.
    QLabel* createDeveloperHintLabel(const QString& hint, QWidget* parent);
    void registerDeveloperHint(QWidget* control, QLabel* label);
    QLabel* addWidgetWithHint(QBoxLayout* layout, QWidget* control, const QString& hint);
    QLabel* addFormRowWithHint(QFormLayout* form, const QString& labelText, QWidget* control,
                               const QString& hint);
    void addHintRow(QFormLayout* form, QWidget* control, const QString& hint);
    void updateDeveloperHints();

    QLineEdit* m_hostEdit = nullptr;
    QSpinBox* m_portSpin = nullptr;
    QLineEdit* m_userEdit = nullptr;
    QLineEdit* m_domainEdit = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
    QComboBox* m_resolutionCombo = nullptr;
    QLineEdit* m_profileNameEdit = nullptr;
    QLineEdit* m_drivesEdit = nullptr;
    QLineEdit* m_gatewayHostEdit = nullptr;
    QLineEdit* m_gatewayUserEdit = nullptr;
    QLineEdit* m_remoteProgramEdit = nullptr;
    QSpinBox* m_gatewayPortSpin = nullptr;
    QComboBox* m_colorDepthCombo = nullptr;
    QComboBox* m_networkCombo = nullptr;
    QComboBox* m_authWarningCombo = nullptr;
    QComboBox* m_keyboardCombo = nullptr;
    QComboBox* m_quickHostCombo = nullptr;
    QComboBox* m_scaleCombo = nullptr;
    QComboBox* m_orientationCombo = nullptr;
    QComboBox* m_gdiCombo = nullptr;
    QComboBox* m_compressionCombo = nullptr;
    QComboBox* m_secProtocolCombo = nullptr;
    QComboBox* m_proxyTypeCombo = nullptr;
    QLineEdit* m_clientHostnameEdit = nullptr;
    QLineEdit* m_shellEdit = nullptr;
    QLineEdit* m_shellDirEdit = nullptr;
    QLineEdit* m_proxyHostEdit = nullptr;
    QLineEdit* m_proxyUserEdit = nullptr;
    QLineEdit* m_proxyPasswordEdit = nullptr;
    QSpinBox* m_proxyPortSpin = nullptr;
    QSpinBox* m_timeoutSpin = nullptr;
    QSpinBox* m_tlsSeclevelSpin = nullptr;
    QSpinBox* m_preventLockSpin = nullptr;
    QSpinBox* m_autoReconnectRetriesSpin = nullptr;
    QCheckBox* m_rememberUserCheck = nullptr;
    QCheckBox* m_rememberPasswordCheck = nullptr;
    QCheckBox* m_clipboardCheck = nullptr;
    QCheckBox* m_drivesCheck = nullptr;
    QCheckBox* m_audioCheck = nullptr;
    QCheckBox* m_microphoneCheck = nullptr;
    QCheckBox* m_printersCheck = nullptr;
    QCheckBox* m_homeDriveCheck = nullptr;
    QCheckBox* m_dynamicResolutionCheck = nullptr;
    QCheckBox* m_multimonCheck = nullptr;
    QCheckBox* m_connectionBarCheck = nullptr;
    QCheckBox* m_wallpaperCheck = nullptr;
    QCheckBox* m_fontSmoothingCheck = nullptr;
    QCheckBox* m_desktopCompositionCheck = nullptr;
    QCheckBox* m_animationsCheck = nullptr;
    QCheckBox* m_bitmapCacheCheck = nullptr;
    QCheckBox* m_reconnectCheck = nullptr;
    QCheckBox* m_gatewayCheck = nullptr;
    QCheckBox* m_gatewaySameCredentialsCheck = nullptr;
    QCheckBox* m_remoteAppCheck = nullptr;
    QCheckBox* m_redirectPortsCheck = nullptr;
    QCheckBox* m_redirectSmartCardsCheck = nullptr;
    QCheckBox* m_webAccountCheck = nullptr;
    QCheckBox* m_developerModeCheck = nullptr;
    QCheckBox* m_spanMonitorsCheck = nullptr;
    QCheckBox* m_smartSizingCheck = nullptr;
    QCheckBox* m_clipboardFilesCheck = nullptr;
    QCheckBox* m_multitouchCheck = nullptr;
    QCheckBox* m_windowDragCheck = nullptr;
    QCheckBox* m_rfxCheck = nullptr;
    QCheckBox* m_nscCheck = nullptr;
    QCheckBox* m_jpegCheck = nullptr;
    QCheckBox* m_videoCheck = nullptr;
    QCheckBox* m_adminCheck = nullptr;
    QCheckBox* m_restrictedAdminCheck = nullptr;
    QCheckBox* m_workareaCheck = nullptr;
    QPlainTextEdit* m_commandPreviewEdit = nullptr;
    QTimer* m_commandPreviewTimer = nullptr;
    QLabel* m_errorLabel = nullptr;
    QLabel* m_passwordHint = nullptr;
    QLabel* m_quickUserLabel = nullptr;
    QDialogButtonBox* m_buttonBox = nullptr;
    QListWidget* m_profileList = nullptr;
    QTabWidget* m_tabs = nullptr;
    QWidget* m_optionsPanel = nullptr;
    QWidget* m_quickPanel = nullptr;
    QToolButton* m_optionsToggle = nullptr;

    QToolButton* m_newButton = nullptr;
    QToolButton* m_duplicateButton = nullptr;
    QToolButton* m_deleteButton = nullptr;
    QToolButton* m_upButton = nullptr;
    QToolButton* m_downButton = nullptr;
    QToolButton* m_importButton = nullptr;
    QToolButton* m_exportButton = nullptr;
    QToolButton* m_saveButton = nullptr;
    QToolButton* m_saveAsButton = nullptr;

    ProfileStore* m_store = nullptr;
    ConnectionProfile m_profile;
    QString m_selectedId;
    bool m_populating = false;
    QList<QPair<QWidget*, QLabel*>> m_developerHints;
};

} // namespace qtrdp
