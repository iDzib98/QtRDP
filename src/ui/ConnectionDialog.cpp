#include "ui/ConnectionDialog.h"

#include "core/CredentialStore.h"
#include "core/RdpFile.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QShowEvent>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

namespace qtrdp {

namespace {

QString settingsFilePath()
{
    return ProfileStore::defaultFilePath();
}

} // namespace

ConnectionDialog::ConnectionDialog(QWidget* parent) : QDialog(parent), m_store(new ProfileStore)
{
    setWindowTitle(QStringLiteral("Conexión a Escritorio remoto"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/qtrdp.svg")));
    buildUi();
    selectLastUsedProfile();
    updateOkButton();
    updatePasswordHint();
}

ConnectionDialog::~ConnectionDialog()
{
    delete m_store;
}

void ConnectionDialog::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto* banner = new QWidget(this);
    auto* bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(8, 2, 8, 4);
    bannerLayout->setSpacing(12);

    auto* bannerIcon = new QLabel(banner);
    bannerIcon->setObjectName(QStringLiteral("connectionBannerIcon"));
    bannerIcon->setPixmap(windowIcon().pixmap(48, 48));
    bannerLayout->addWidget(bannerIcon, 0, Qt::AlignVCenter);

    auto* bannerTitle = new QLabel(
        QStringLiteral("<span style='color:#0754a6'>Escritorio remoto</span><br>"
                       "<b style='color:#0754a6'>Conexión</b>"), banner);
    bannerTitle->setObjectName(QStringLiteral("connectionBannerTitle"));
    QFont bannerFont = bannerTitle->font();
    bannerFont.setPointSize(bannerFont.pointSize() + 5);
    bannerTitle->setFont(bannerFont);
    bannerLayout->addWidget(bannerTitle, 1, Qt::AlignVCenter);
    layout->addWidget(banner);

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);
    layout->addWidget(divider);

    m_quickPanel = new QWidget(this);
    m_quickPanel->setObjectName(QStringLiteral("quickConnectionPanel"));
    auto* quickLayout = new QVBoxLayout(m_quickPanel);
    quickLayout->setContentsMargins(2, 8, 2, 4);
    quickLayout->setSpacing(5);

    auto* hostRow = new QHBoxLayout;
    auto* hostLabel = new QLabel(QStringLiteral("Equipo:"), m_quickPanel);
    hostLabel->setMinimumWidth(66);
    m_quickHostCombo = new QComboBox(m_quickPanel);
    m_quickHostCombo->setObjectName(QStringLiteral("quickHostCombo"));
    m_quickHostCombo->setEditable(true);
    m_quickHostCombo->setInsertPolicy(QComboBox::NoInsert);
    m_quickHostCombo->lineEdit()->setObjectName(QStringLiteral("quickHostEdit"));
    m_quickHostCombo->lineEdit()->setPlaceholderText(
        QStringLiteral("Ejemplo: equipo.fabrikam.com"));
    hostRow->addWidget(hostLabel);
    hostRow->addWidget(m_quickHostCombo, 1);
    quickLayout->addLayout(hostRow);

    m_quickUserLabel = new QLabel(QStringLiteral("Usuario:    Ninguno especificado"),
                                  m_quickPanel);
    m_quickUserLabel->setObjectName(QStringLiteral("quickUserLabel"));
    quickLayout->addWidget(m_quickUserLabel);

    auto* hint = new QLabel(
        QStringLiteral("El campo del nombre de equipo está en blanco. Escriba un "
                       "nombre de equipo remoto completo."), m_quickPanel);
    hint->setObjectName(QStringLiteral("quickConnectionHint"));
    hint->setWordWrap(true);
    quickLayout->addWidget(hint);
    layout->addWidget(m_quickPanel);

    m_optionsPanel = new QWidget(this);
    m_optionsPanel->setObjectName(QStringLiteral("connectionOptionsPanel"));
    auto* optionsLayout = new QVBoxLayout(m_optionsPanel);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(4);
    m_tabs = new QTabWidget(m_optionsPanel);
    m_tabs->setObjectName(QStringLiteral("connectionOptionsTabs"));
    m_tabs->addTab(buildForm(), QStringLiteral("General"));
    m_tabs->addTab(buildScreenPage(), QStringLiteral("Pantalla"));
    m_tabs->addTab(buildLocalResourcesPage(), QStringLiteral("Recursos locales"));
    m_tabs->addTab(buildPerformancePage(), QStringLiteral("Rendimiento"));
    m_tabs->addTab(buildAdvancedPage(), QStringLiteral("Opciones avanzadas"));
    optionsLayout->addWidget(m_tabs);
    layout->addWidget(m_optionsPanel, 1);
    m_optionsPanel->hide();

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("errorLabel"));
    m_errorLabel->setStyleSheet(QStringLiteral("color: #b02020;"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    layout->addWidget(m_errorLabel);

    auto* footer = new QHBoxLayout;
    m_optionsToggle = new QToolButton(this);
    m_optionsToggle->setObjectName(QStringLiteral("optionsToggleButton"));
    m_optionsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_optionsToggle->setArrowType(Qt::DownArrow);
    m_optionsToggle->setText(QStringLiteral("Mostrar opciones"));
    footer->addWidget(m_optionsToggle);
    footer->addStretch(1);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Help, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Conectar"));
    m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    m_buttonBox->button(QDialogButtonBox::Help)->setText(QStringLiteral("Ayuda"));
    footer->addWidget(m_buttonBox);
    layout->addLayout(footer);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &ConnectionDialog::accept);
    connect(m_buttonBox->button(QDialogButtonBox::Help), &QPushButton::clicked, this, [this]()
    {
        QMessageBox::information(
            this, QStringLiteral("Conexión a Escritorio remoto"),
            QStringLiteral("Escribe el nombre o la dirección del equipo y pulsa "
                           "Conectar. En Mostrar opciones puedes ajustar la pantalla, "
                           "los recursos locales, el rendimiento y la puerta de enlace."));
    });
    connect(m_optionsToggle, &QToolButton::clicked, this, [this]()
    {
        setOptionsExpanded(!m_optionsPanel->isVisible());
    });

    connect(m_hostEdit, &QLineEdit::textChanged, this, &ConnectionDialog::updateOkButton);
    connect(m_hostEdit, &QLineEdit::textChanged, m_quickHostCombo->lineEdit(),
            &QLineEdit::setText);
    connect(m_quickHostCombo->lineEdit(), &QLineEdit::textChanged, m_hostEdit,
            &QLineEdit::setText);
    connect(m_userEdit, &QLineEdit::textChanged, this, &ConnectionDialog::updateQuickUserLabel);
    connect(m_domainEdit, &QLineEdit::textChanged, this,
            &ConnectionDialog::updateQuickUserLabel);
    connect(m_hostEdit, &QLineEdit::returnPressed, this, [this]()
    {
        if (isVisible() && m_buttonBox->button(QDialogButtonBox::Ok)->isEnabled())
            accept();
    });

    resize(430, 285);
}

QWidget* ConnectionDialog::buildProfileList()
{
    auto* panel = new QVBoxLayout;
    panel->setSpacing(6);

    auto* label = new QLabel(QStringLiteral("Perfiles guardados"), this);
    QFont labelFont = label->font();
    labelFont.setBold(true);
    label->setFont(labelFont);
    panel->addWidget(label);

    m_profileList = new QListWidget(this);
    m_profileList->setObjectName(QStringLiteral("profileList"));
    m_profileList->setIconSize(QSize(16, 16));
    m_profileList->setMinimumWidth(200);
    panel->addWidget(m_profileList, 1);

    const auto makeButton = [this](QStyle::StandardPixmap icon, const QString& tooltip)
    {
        auto* button = new QToolButton(this);
        button->setIcon(style()->standardIcon(icon));
        button->setToolTip(tooltip);
        button->setAutoRaise(true);
        return button;
    };

    m_newButton = makeButton(QStyle::SP_FileIcon, QStringLiteral("Crear un perfil nuevo"));
    m_newButton->setObjectName(QStringLiteral("newProfileButton"));
    m_duplicateButton =
	makeButton(QStyle::SP_FileDialogNewFolder, QStringLiteral("Duplicar el perfil"));
    m_deleteButton = makeButton(QStyle::SP_TrashIcon, QStringLiteral("Eliminar el perfil"));
    m_deleteButton->setObjectName(QStringLiteral("deleteProfileButton"));
    m_upButton = makeButton(QStyle::SP_ArrowUp, QStringLiteral("Subir en la lista"));
    m_upButton->setObjectName(QStringLiteral("moveUpButton"));
    m_downButton = makeButton(QStyle::SP_ArrowDown, QStringLiteral("Bajar en la lista"));
    m_downButton->setObjectName(QStringLiteral("moveDownButton"));
    m_importButton = makeButton(QStyle::SP_DialogOpenButton,
				QStringLiteral("Importar un archivo .rdp"));
    m_importButton->setObjectName(QStringLiteral("importRdpButton"));
    m_exportButton = makeButton(QStyle::SP_DialogSaveButton,
				QStringLiteral("Exportar el perfil como archivo .rdp"));
    m_exportButton->setObjectName(QStringLiteral("exportRdpButton"));

    auto* editRow = new QHBoxLayout;
    editRow->setSpacing(2);
    editRow->addWidget(m_newButton);
    editRow->addWidget(m_duplicateButton);
    editRow->addWidget(m_deleteButton);
    editRow->addStretch(1);
    editRow->addWidget(m_upButton);
    editRow->addWidget(m_downButton);
    panel->addLayout(editRow);

    auto* fileRow = new QHBoxLayout;
    fileRow->setSpacing(2);
    m_saveButton = new QToolButton(this);
    m_saveButton->setObjectName(QStringLiteral("saveProfileButton"));
    m_saveButton->setText(QStringLiteral("Guardar"));
    m_saveButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_saveAsButton = new QToolButton(this);
    m_saveAsButton->setObjectName(QStringLiteral("saveAsProfileButton"));
    m_saveAsButton->setText(QStringLiteral("Guardar como…"));
    m_saveAsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    fileRow->addWidget(m_saveButton);
    fileRow->addWidget(m_saveAsButton);
    fileRow->addWidget(m_importButton);
    fileRow->addWidget(m_exportButton);
    fileRow->addStretch(1);
    panel->addLayout(fileRow);

    connect(m_profileList, &QListWidget::currentItemChanged, this,
	    [this](QListWidgetItem* current)
	    {
		if (m_populating || !current)
		    return;
		selectProfile(current->data(Qt::UserRole).toString());
	    });

    connect(m_newButton, &QToolButton::clicked, this, &ConnectionDialog::onNewProfile);
    connect(m_duplicateButton, &QToolButton::clicked, this,
	    &ConnectionDialog::onDuplicateProfile);
    connect(m_deleteButton, &QToolButton::clicked, this, &ConnectionDialog::onDeleteProfile);
    connect(m_upButton, &QToolButton::clicked, this, [this]()
	    { onMoveProfile(-1); });
    connect(m_downButton, &QToolButton::clicked, this, [this]()
	    { onMoveProfile(1); });
    connect(m_importButton, &QToolButton::clicked, this, &ConnectionDialog::onImportRdp);
    connect(m_exportButton, &QToolButton::clicked, this, &ConnectionDialog::onExportRdp);
    connect(m_saveButton, &QToolButton::clicked, this, [this]() { persistProfile(); });
    connect(m_saveAsButton, &QToolButton::clicked, this, &ConnectionDialog::saveAsNewProfile);

    auto* host = new QWidget(this);
    host->setLayout(panel);
    return host;
}

QWidget* ConnectionDialog::buildForm()
{
    auto* page = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(8, 8, 8, 8);
    pageLayout->setSpacing(8);

    auto* sessionGroup = new QGroupBox(QStringLiteral("Configuración del inicio de sesión"), page);
    auto* form = new QFormLayout(sessionGroup);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setContentsMargins(12, 18, 12, 10);

    m_profileNameEdit = new QLineEdit(this);
    m_profileNameEdit->setObjectName(QStringLiteral("profileNameEdit"));
    m_profileNameEdit->setPlaceholderText(QStringLiteral("nombre del perfil"));
    m_profileNameEdit->setClearButtonEnabled(true);

    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setObjectName(QStringLiteral("hostEdit"));
    m_hostEdit->setPlaceholderText(QStringLiteral("Ejemplo: equipo.fabrikam.com"));
    m_hostEdit->setClearButtonEnabled(true);
    form->addRow(QStringLiteral("Equipo:"), m_hostEdit);

    auto* portRow = new QHBoxLayout;
    m_portSpin = new QSpinBox(this);
    m_portSpin->setObjectName(QStringLiteral("portSpin"));
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(3389);
    portRow->addWidget(m_portSpin);
    portRow->addStretch(1);
    form->addRow(QStringLiteral("Puerto:"), portRow);

    m_userEdit = new QLineEdit(this);
    m_userEdit->setObjectName(QStringLiteral("userEdit"));
    m_userEdit->setPlaceholderText(QStringLiteral("usuario"));
    form->addRow(QStringLiteral("Usuario:"), m_userEdit);

    m_domainEdit = new QLineEdit(this);
    m_domainEdit->setObjectName(QStringLiteral("domainEdit"));
    m_domainEdit->setPlaceholderText(QStringLiteral("opcional — dejar vacío si no hay dominio"));
    form->addRow(QStringLiteral("Dominio:"), m_domainEdit);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setObjectName(QStringLiteral("passwordEdit"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("contraseña"));
    form->addRow(QStringLiteral("Contraseña:"), m_passwordEdit);

    m_passwordHint = new QLabel(this);
    m_passwordHint->setObjectName(QStringLiteral("passwordHint"));
    m_passwordHint->setStyleSheet(QStringLiteral("color: #666;"));
    m_passwordHint->setWordWrap(true);
    form->addRow(QString(), m_passwordHint);

    m_rememberUserCheck = new QCheckBox(QStringLiteral("Recordar mi usuario"), this);
    m_rememberUserCheck->setChecked(true);
    form->addRow(QString(), m_rememberUserCheck);

    m_rememberPasswordCheck = new QCheckBox(QStringLiteral("Recordar contraseña"), this);
    m_rememberPasswordCheck->setObjectName(QStringLiteral("rememberPasswordCheck"));
    form->addRow(QString(), m_rememberPasswordCheck);

    auto* accountGroup = new QGroupBox(QStringLiteral("Configuración de la conexión"), page);
    auto* accountLayout = new QVBoxLayout(accountGroup);
    accountLayout->setContentsMargins(10, 16, 10, 8);
    accountLayout->setSpacing(5);
    auto* profileNameRow = new QFormLayout;
    profileNameRow->addRow(QStringLiteral("Nombre del perfil:"), m_profileNameEdit);
    accountLayout->addLayout(profileNameRow);
    accountLayout->addWidget(buildProfileList(), 1);

    pageLayout->addWidget(sessionGroup);
    pageLayout->addWidget(accountGroup, 1);

    connect(m_profileNameEdit, &QLineEdit::textChanged, this,
	    &ConnectionDialog::onProfileNameChanged);
    connect(m_profileNameEdit, &QLineEdit::editingFinished, this, [this]()
	    {
		if (m_populating || m_selectedId.isEmpty())
		    return;

		ConnectionProfile edited = m_store->profile(m_selectedId);
		const QString newName = m_profileNameEdit->text().trimmed();
		if (edited.name == newName)
		    return;

		edited.name = newName.isEmpty() ? edited.displayName() : newName;
		if (m_store->update(edited))
		{
		    m_profile.name = edited.name;
		    if (QListWidgetItem* item = m_profileList->currentItem())
			item->setText(edited.name);
		}
	    });
    connect(m_userEdit, &QLineEdit::textChanged, this, &ConnectionDialog::updatePasswordHint);
    connect(m_domainEdit, &QLineEdit::textChanged, this, &ConnectionDialog::updatePasswordHint);
    connect(m_rememberPasswordCheck, &QCheckBox::toggled, this,
	    &ConnectionDialog::updatePasswordHint);
    connect(m_passwordEdit, &QLineEdit::textChanged, this,
	    &ConnectionDialog::updatePasswordHint);

    return page;
}

QWidget* ConnectionDialog::buildScreenPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* screenGroup = new QGroupBox(QStringLiteral("Configuración de pantalla"), page);
    auto* screenForm = new QFormLayout(screenGroup);
    screenForm->setContentsMargins(12, 18, 12, 10);
    m_resolutionCombo = new QComboBox(screenGroup);
    m_resolutionCombo->setObjectName(QStringLiteral("resolutionCombo"));
    m_resolutionCombo->addItem(QStringLiteral("Pantalla completa"), QStringLiteral("fullscreen"));
    m_resolutionCombo->addItem(QStringLiteral("Automática"), QStringLiteral("automatic"));
    m_resolutionCombo->addItem(QStringLiteral("1024 × 768"));
    m_resolutionCombo->addItem(QStringLiteral("1280 × 800"));
    m_resolutionCombo->addItem(QStringLiteral("1366 × 768"));
    m_resolutionCombo->addItem(QStringLiteral("1440 × 900"));
    m_resolutionCombo->addItem(QStringLiteral("1600 × 900"));
    m_resolutionCombo->addItem(QStringLiteral("1920 × 1080"));
    m_resolutionCombo->addItem(QStringLiteral("2560 × 1440"));
    m_resolutionCombo->addItem(QStringLiteral("3840 × 2160"));
    screenForm->addRow(QStringLiteral("Tamaño del escritorio remoto:"), m_resolutionCombo);

    m_colorDepthCombo = new QComboBox(screenGroup);
    m_colorDepthCombo->setObjectName(QStringLiteral("colorDepthCombo"));
    m_colorDepthCombo->addItem(QStringLiteral("Calidad más alta (32 bits)"), 32);
    m_colorDepthCombo->addItem(QStringLiteral("Alta calidad (24 bits)"), 24);
    m_colorDepthCombo->addItem(QStringLiteral("Media (16 bits)"), 16);
    m_colorDepthCombo->addItem(QStringLiteral("Baja (8 bits)"), 8);
    screenForm->addRow(QStringLiteral("Colores:"), m_colorDepthCombo);

    m_dynamicResolutionCheck = new QCheckBox(
        QStringLiteral("Cambiar el tamaño del escritorio remoto al cambiar el tamaño de la ventana"),
        screenGroup);
    m_dynamicResolutionCheck->setObjectName(QStringLiteral("dynamicResolutionCheck"));
    screenForm->addRow(QString(), m_dynamicResolutionCheck);
    m_multimonCheck = new QCheckBox(
        QStringLiteral("Usar todos mis monitores para la sesión remota"), screenGroup);
    screenForm->addRow(QString(), m_multimonCheck);
    m_connectionBarCheck = new QCheckBox(
        QStringLiteral("Mostrar la barra de conexión cuando se use pantalla completa"),
        screenGroup);
    screenForm->addRow(QString(), m_connectionBarCheck);

    layout->addWidget(screenGroup);
    layout->addStretch(1);
    return page;
}

QWidget* ConnectionDialog::buildLocalResourcesPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* audioGroup = new QGroupBox(QStringLiteral("Audio remoto"), page);
    auto* audioLayout = new QVBoxLayout(audioGroup);
    m_audioCheck = new QCheckBox(QStringLiteral("Reproducir el audio en este equipo"), audioGroup);
    m_audioCheck->setObjectName(QStringLiteral("redirectAudioCheck"));
    m_microphoneCheck = new QCheckBox(QStringLiteral("Grabar desde este equipo"), audioGroup);
    audioLayout->addWidget(m_audioCheck);
    audioLayout->addWidget(m_microphoneCheck);
    layout->addWidget(audioGroup);

    auto* keyboardGroup = new QGroupBox(QStringLiteral("Teclado"), page);
    auto* keyboardForm = new QFormLayout(keyboardGroup);
    m_keyboardCombo = new QComboBox(keyboardGroup);
    m_keyboardCombo->setObjectName(QStringLiteral("keyboardCombo"));
    m_keyboardCombo->addItem(QStringLiteral("Solo cuando se use la pantalla completa"),
                             QStringLiteral("fullscreen"));
    m_keyboardCombo->addItem(QStringLiteral("En este equipo"), QStringLiteral("local"));
    m_keyboardCombo->addItem(QStringLiteral("En el equipo remoto"), QStringLiteral("remote"));
    keyboardForm->addRow(QStringLiteral("Aplicar combinaciones de teclas de Windows:"),
                         m_keyboardCombo);
    layout->addWidget(keyboardGroup);

    auto* devicesGroup = new QGroupBox(QStringLiteral("Dispositivos y recursos locales"), page);
    auto* devicesLayout = new QVBoxLayout(devicesGroup);
    m_clipboardCheck = new QCheckBox(QStringLiteral("Portapapeles"), devicesGroup);
    m_drivesCheck = new QCheckBox(QStringLiteral("Unidades"), devicesGroup);
    m_printersCheck = new QCheckBox(QStringLiteral("Impresoras"), devicesGroup);
    m_homeDriveCheck = new QCheckBox(QStringLiteral("Compartir mi carpeta personal"), devicesGroup);
    m_redirectPortsCheck = new QCheckBox(QStringLiteral("Puertos"), devicesGroup);
    m_redirectSmartCardsCheck = new QCheckBox(QStringLiteral("Tarjetas inteligentes"), devicesGroup);
    devicesLayout->addWidget(m_clipboardCheck);
    devicesLayout->addWidget(m_drivesCheck);
    auto* drivesForm = new QFormLayout;
    m_drivesEdit = new QLineEdit(devicesGroup);
    m_drivesEdit->setObjectName(QStringLiteral("drivesEdit"));
    m_drivesEdit->setPlaceholderText(QStringLiteral("nombre,ruta; por ejemplo: home,/home/usuario"));
    drivesForm->addRow(QStringLiteral("Unidades específicas:"), m_drivesEdit);
    devicesLayout->addLayout(drivesForm);
    devicesLayout->addWidget(m_homeDriveCheck);
    devicesLayout->addWidget(m_printersCheck);
    devicesLayout->addWidget(m_redirectPortsCheck);
    devicesLayout->addWidget(m_redirectSmartCardsCheck);
    layout->addWidget(devicesGroup, 1);
    return page;
}

QWidget* ConnectionDialog::buildPerformancePage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* performanceGroup = new QGroupBox(QStringLiteral("Rendimiento"), page);
    auto* performanceLayout = new QVBoxLayout(performanceGroup);
    auto* networkForm = new QFormLayout;
    m_networkCombo = new QComboBox(performanceGroup);
    m_networkCombo->setObjectName(QStringLiteral("networkCombo"));
    m_networkCombo->addItem(QStringLiteral("Detectar calidad de conexión automáticamente"),
                            QStringLiteral("auto"));
    m_networkCombo->addItem(QStringLiteral("Módem (56 Kbps)"), QStringLiteral("modem"));
    m_networkCombo->addItem(QStringLiteral("Banda ancha (256 Kbps - 2 Mbps)"),
                            QStringLiteral("broadband"));
    m_networkCombo->addItem(QStringLiteral("LAN (10 Mbps o superior)"), QStringLiteral("lan"));
    networkForm->addRow(QStringLiteral("Velocidad de conexión:"), m_networkCombo);
    performanceLayout->addLayout(networkForm);

    m_wallpaperCheck = new QCheckBox(QStringLiteral("Fondo de escritorio"), performanceGroup);
    m_fontSmoothingCheck = new QCheckBox(QStringLiteral("Suavizado de fuentes"), performanceGroup);
    m_desktopCompositionCheck = new QCheckBox(QStringLiteral("Composición del escritorio"),
                                               performanceGroup);
    m_animationsCheck = new QCheckBox(QStringLiteral("Animaciones de menús y ventanas"),
                                       performanceGroup);
    m_bitmapCacheCheck = new QCheckBox(
        QStringLiteral("Almacenamiento en caché persistente de mapas de bits"), performanceGroup);
    m_reconnectCheck = new QCheckBox(QStringLiteral("Volver a conectarse si se pierde la conexión"),
                                      performanceGroup);
    performanceLayout->addWidget(m_wallpaperCheck);
    performanceLayout->addWidget(m_fontSmoothingCheck);
    performanceLayout->addWidget(m_desktopCompositionCheck);
    performanceLayout->addWidget(m_animationsCheck);
    performanceLayout->addWidget(m_bitmapCacheCheck);
    performanceLayout->addWidget(m_reconnectCheck);
    layout->addWidget(performanceGroup);
    layout->addStretch(1);
    return page;
}

QWidget* ConnectionDialog::buildAdvancedPage()
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* authGroup = new QGroupBox(QStringLiteral("Autenticación del servidor"), page);
    auto* authLayout = new QFormLayout(authGroup);
    m_authWarningCombo = new QComboBox(authGroup);
    m_authWarningCombo->setObjectName(QStringLiteral("authWarningCombo"));
    m_authWarningCombo->addItem(QStringLiteral("Avisarme si falla la autenticación"), 1);
    m_authWarningCombo->addItem(QStringLiteral("No conectarse si falla la autenticación"), 2);
    m_authWarningCombo->addItem(QStringLiteral("Conectarse aunque falle la autenticación"), 0);
    authLayout->addRow(QStringLiteral("Si se producen errores:"), m_authWarningCombo);
    layout->addWidget(authGroup);

    auto* gatewayGroup = new QGroupBox(QStringLiteral("Conectarse desde cualquier lugar"), page);
    auto* gatewayLayout = new QFormLayout(gatewayGroup);
    m_gatewayCheck = new QCheckBox(
        QStringLiteral("Usar una puerta de enlace de Escritorio remoto"), gatewayGroup);
    gatewayLayout->addRow(QString(), m_gatewayCheck);
    m_gatewayHostEdit = new QLineEdit(gatewayGroup);
    m_gatewayHostEdit->setObjectName(QStringLiteral("gatewayHostEdit"));
    gatewayLayout->addRow(QStringLiteral("Servidor de puerta de enlace:"), m_gatewayHostEdit);
    m_gatewayPortSpin = new QSpinBox(gatewayGroup);
    m_gatewayPortSpin->setObjectName(QStringLiteral("gatewayPortSpin"));
    m_gatewayPortSpin->setRange(1, 65535);
    m_gatewayPortSpin->setValue(443);
    gatewayLayout->addRow(QStringLiteral("Puerto:"), m_gatewayPortSpin);
    m_gatewaySameCredentialsCheck = new QCheckBox(
        QStringLiteral("Usar las mismas credenciales para la puerta de enlace"), gatewayGroup);
    gatewayLayout->addRow(QString(), m_gatewaySameCredentialsCheck);
    m_gatewayUserEdit = new QLineEdit(gatewayGroup);
    m_gatewayUserEdit->setObjectName(QStringLiteral("gatewayUserEdit"));
    gatewayLayout->addRow(QStringLiteral("Usuario de puerta de enlace:"), m_gatewayUserEdit);
    layout->addWidget(gatewayGroup);

    auto* appGroup = new QGroupBox(QStringLiteral("Autenticación del usuario"), page);
    auto* appLayout = new QFormLayout(appGroup);
    m_webAccountCheck = new QCheckBox(
        QStringLiteral("Usar una cuenta web para iniciar sesión en el equipo remoto"), appGroup);
    appLayout->addRow(QString(), m_webAccountCheck);
    m_remoteAppCheck = new QCheckBox(QStringLiteral("Iniciar una aplicación remota"), appGroup);
    appLayout->addRow(QString(), m_remoteAppCheck);
    m_remoteProgramEdit = new QLineEdit(appGroup);
    m_remoteProgramEdit->setObjectName(QStringLiteral("remoteProgramEdit"));
    appLayout->addRow(QStringLiteral("Programa remoto:"), m_remoteProgramEdit);
    layout->addWidget(appGroup);
    layout->addStretch(1);

    connect(m_gatewayCheck, &QCheckBox::toggled, m_gatewayHostEdit, &QLineEdit::setEnabled);
    connect(m_gatewayCheck, &QCheckBox::toggled, m_gatewayPortSpin, &QSpinBox::setEnabled);
    connect(m_gatewayCheck, &QCheckBox::toggled, m_gatewaySameCredentialsCheck,
            &QCheckBox::setEnabled);
    connect(m_gatewayCheck, &QCheckBox::toggled, m_gatewayUserEdit, &QLineEdit::setEnabled);
    connect(m_gatewaySameCredentialsCheck, &QCheckBox::toggled, m_gatewayUserEdit,
            [this](bool same) { m_gatewayUserEdit->setEnabled(m_gatewayCheck->isChecked() && !same); });
    connect(m_remoteAppCheck, &QCheckBox::toggled, m_remoteProgramEdit, &QLineEdit::setEnabled);
    return page;
}

void ConnectionDialog::refreshProfileList(const QString& selectId)
{
    const QString target = selectId.isEmpty() ? m_selectedId : selectId;

    m_populating = true;
    m_profileList->clear();
    const QList<ConnectionProfile> profiles = m_store->profiles();
    if (m_quickHostCombo)
    {
        const QString currentHost = m_quickHostCombo->currentText();
        m_quickHostCombo->clear();
        for (const ConnectionProfile& profile : profiles)
        {
            if (!profile.host.isEmpty() && m_quickHostCombo->findText(profile.host) < 0)
                m_quickHostCombo->addItem(profile.host);
        }
        m_quickHostCombo->setEditText(currentHost);
    }

    for (const ConnectionProfile& profile : profiles)
    {
        auto* item = new QListWidgetItem(profile.displayName(), m_profileList);
        item->setData(Qt::UserRole, profile.id);
        const QString detail =
	    profile.host.isEmpty() ? profile.displayName() : profile.host;
        item->setToolTip(QStringLiteral("%1\n%2").arg(profile.displayName(), detail));
    }

    int index = -1;
    if (!target.isEmpty())
        index = m_store->indexOf(target);
    if (index < 0 && !profiles.isEmpty())
        index = 0;

    m_populating = false;

    if (index >= 0)
        m_profileList->setCurrentRow(index);

    updateActionButtons();

    if (index >= 0)
        selectProfile(profiles.at(index).id);
    else
    {
        m_selectedId.clear();
        loadProfileIntoForm(ConnectionProfile());
    }
}

void ConnectionDialog::selectLastUsedProfile()
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    const QString id = settings.value(QStringLiteral("app/lastProfileId")).toString();
    if (!id.isEmpty() && m_store->contains(id))
    {
        refreshProfileList(id);
        return;
    }

    refreshProfileList();
}

void ConnectionDialog::selectProfile(const QString& id)
{
    if (id.isEmpty())
        return;

    m_selectedId = id;
    loadProfileIntoForm(m_store->profile(id));
    updateActionButtons();
}

void ConnectionDialog::loadProfileIntoForm(const ConnectionProfile& profile)
{
    if (!m_profileNameEdit || !m_hostEdit || !m_portSpin)
        return;

    m_populating = true;

    m_profile = profile;
    m_profileNameEdit->setText(profile.name);
    m_hostEdit->setText(profile.host);
    m_portSpin->setValue(profile.port > 0 ? profile.port : 3389);
    m_userEdit->setText(profile.username);
    m_domainEdit->setText(profile.domain);
    m_passwordEdit->clear();
    m_rememberUserCheck->setChecked(profile.rememberUsername);
    m_rememberPasswordCheck->setChecked(profile.rememberPassword);
    m_clipboardCheck->setChecked(profile.redirectClipboard);
    m_drivesCheck->setChecked(profile.redirectDrives);
    m_printersCheck->setChecked(profile.redirectPrinters);
    m_homeDriveCheck->setChecked(profile.redirectHomeDrive);
    m_drivesEdit->setText(profile.drives.join(QLatin1Char(';')));
    m_audioCheck->setChecked(profile.redirectAudio);
    m_microphoneCheck->setChecked(profile.redirectMicrophone);
    m_dynamicResolutionCheck->setChecked(profile.dynamicResolution);
    m_multimonCheck->setChecked(profile.useMultimon);
    m_connectionBarCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("showConnectionBar"), true).toBool());
    m_colorDepthCombo->setCurrentIndex(qMax(0, m_colorDepthCombo->findData(profile.colorDepth)));

    const QString network = profile.networkType;
    int networkIndex = m_networkCombo->findData(network);
    if (networkIndex < 0)
        networkIndex = m_networkCombo->findData(QStringLiteral("auto"));
    m_networkCombo->setCurrentIndex(networkIndex);
    m_wallpaperCheck->setChecked(profile.wallpaper != QLatin1String("0"));
    m_fontSmoothingCheck->setChecked(profile.fontSmoothing);
    m_desktopCompositionCheck->setChecked(profile.desktopComposition);
    m_animationsCheck->setChecked(profile.animations);
    m_bitmapCacheCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("bitmapCache"), true).toBool());
    m_reconnectCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("autoReconnect"), true).toBool());

    m_gatewayCheck->setChecked(profile.gatewayEnabled);
    m_gatewayHostEdit->setText(profile.gatewayHostname);
    m_gatewayPortSpin->setValue(profile.gatewayPort > 0 ? profile.gatewayPort : 443);
    m_gatewaySameCredentialsCheck->setChecked(profile.gatewaySameCredentials);
    m_gatewayUserEdit->setText(profile.gatewayUsername);
    m_gatewayHostEdit->setEnabled(profile.gatewayEnabled);
    m_gatewaySameCredentialsCheck->setEnabled(profile.gatewayEnabled);
    m_gatewayUserEdit->setEnabled(profile.gatewayEnabled && !profile.gatewaySameCredentials);
    m_remoteProgramEdit->setText(profile.remoteProgram);
    m_remoteAppCheck->setChecked(!profile.remoteProgram.isEmpty());
    m_remoteProgramEdit->setEnabled(m_remoteAppCheck->isChecked());
    m_webAccountCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("webAccount"), false).toBool());
    m_redirectPortsCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("redirectPorts"), false).toBool());
    m_redirectSmartCardsCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("redirectSmartCards"), false).toBool());

    const QString keyboard = profile.extraOptions.value(QStringLiteral("keyboardHook"),
                                                         QStringLiteral("fullscreen")).toString();
    m_keyboardCombo->setCurrentIndex(qMax(0, m_keyboardCombo->findData(keyboard)));
    m_authWarningCombo->setCurrentIndex(qMax(
        0, m_authWarningCombo->findData(
               profile.extraOptions.value(QStringLiteral("authenticationLevel"), 1))));

    m_resolutionCombo->setCurrentIndex(1);
    if (profile.extraOptions.value(QStringLiteral("fullScreen"), false).toBool())
        m_resolutionCombo->setCurrentIndex(0);
    if (profile.desktopWidth > 0 && profile.desktopHeight > 0)
    {
        const QString text = QStringLiteral("%1 × %2").arg(profile.desktopWidth)
					.arg(profile.desktopHeight);
        const int index = m_resolutionCombo->findText(text);
        if (index >= 2)
            m_resolutionCombo->setCurrentIndex(index);
    }

    m_populating = false;

    updateOkButton();
    updatePasswordHint();
}

void ConnectionDialog::loadProfile(const ConnectionProfile& profile)
{
    if (!profile.isValid())
        return;

    // Si el perfil ya está guardado solo lo seleccionamos; si no, lo añadimos.
    QString existingId;
    const QList<ConnectionProfile> profiles = m_store->profiles();
    for (const ConnectionProfile& candidate : profiles)
    {
        if ((candidate.host == profile.host) && (candidate.port == profile.port) &&
	    (candidate.username == profile.username))
	{
	    existingId = candidate.id;
	    break;
	}
    }

    if (!existingId.isEmpty())
    {
        refreshProfileList(existingId);
        return;
    }

    ConnectionProfile stored = profile;
    if (stored.name.isEmpty())
        stored.name = profile.displayName();
    if (m_store->add(stored))
        refreshProfileList(stored.id);
    else
        loadProfileIntoForm(profile);
}

void ConnectionDialog::collectForm()
{
    m_profile.name = m_profileNameEdit->text().trimmed();
    m_profile.host = m_hostEdit->text().trimmed();
    m_profile.port = static_cast<quint16>(m_portSpin->value());
    m_profile.username = m_userEdit->text().trimmed();
    m_profile.domain = m_domainEdit->text().trimmed();
    m_profile.rememberUsername = m_rememberUserCheck->isChecked();
    m_profile.rememberPassword = m_rememberPasswordCheck->isChecked();
    m_profile.promptForPassword = false;
    m_profile.redirectClipboard = m_clipboardCheck->isChecked();
    m_profile.redirectDrives = m_drivesCheck->isChecked();
    m_profile.redirectPrinters = m_printersCheck->isChecked();
    m_profile.redirectHomeDrive = m_homeDriveCheck->isChecked();
    m_profile.drives = m_drivesEdit->text().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    m_profile.redirectAudio = m_audioCheck->isChecked();
    m_profile.redirectMicrophone = m_microphoneCheck->isChecked();
    m_profile.dynamicResolution = m_dynamicResolutionCheck->isChecked();
    m_profile.useMultimon = m_multimonCheck->isChecked();
    m_profile.colorDepth = m_colorDepthCombo->currentData().toInt();
    m_profile.networkType = m_networkCombo->currentData().toString();
    m_profile.wallpaper = m_wallpaperCheck->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
    m_profile.fontSmoothing = m_fontSmoothingCheck->isChecked();
    m_profile.desktopComposition = m_desktopCompositionCheck->isChecked();
    m_profile.animations = m_animationsCheck->isChecked();
    m_profile.gatewayEnabled = m_gatewayCheck->isChecked();
    m_profile.gatewayHostname = m_gatewayHostEdit->text().trimmed();
    m_profile.gatewayPort = m_gatewayPortSpin->value();
    m_profile.gatewaySameCredentials = m_gatewaySameCredentialsCheck->isChecked();
    m_profile.gatewayUsername = m_gatewayUserEdit->text().trimmed();
    m_profile.remoteProgram = m_remoteAppCheck->isChecked()
                                  ? m_remoteProgramEdit->text().trimmed()
                                  : QString();
    m_profile.extraOptions.insert(QStringLiteral("showConnectionBar"),
                                  m_connectionBarCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("bitmapCache"), m_bitmapCacheCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("autoReconnect"), m_reconnectCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("keyboardHook"), m_keyboardCombo->currentData());
    m_profile.extraOptions.insert(QStringLiteral("authenticationLevel"),
                                  m_authWarningCombo->currentData());
    m_profile.extraOptions.insert(QStringLiteral("webAccount"), m_webAccountCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("redirectPorts"), m_redirectPortsCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("redirectSmartCards"),
                                  m_redirectSmartCardsCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("fullScreen"),
                                  m_resolutionCombo->currentIndex() == 0);

    const int resolutionIndex = m_resolutionCombo->currentIndex();
    if (resolutionIndex >= 2)
    {
        const QStringList parts =
	    m_resolutionCombo->currentText().split(QStringLiteral("×"));
        if (parts.size() == 2)
        {
            m_profile.desktopWidth = parts.at(0).trimmed().toInt();
            m_profile.desktopHeight = parts.at(1).trimmed().toInt();
        }
    }
    else
    {
        m_profile.desktopWidth = 0;
        m_profile.desktopHeight = 0;
    }

    if (m_profile.name.isEmpty())
        m_profile.name = m_profile.displayName();
}

void ConnectionDialog::onProfileNameChanged(const QString& name)
{
    if (m_populating || m_selectedId.isEmpty())
        return;

    QListWidgetItem* item = m_profileList->currentItem();
    if (item)
        item->setText(name.isEmpty() ? m_profile.displayName() : name);
}

void ConnectionDialog::setOptionsExpanded(bool expanded)
{
    if (!m_optionsPanel || !m_quickPanel || !m_optionsToggle)
        return;

    m_optionsPanel->setVisible(expanded);
    m_quickPanel->setVisible(!expanded);
    m_optionsToggle->setArrowType(expanded ? Qt::UpArrow : Qt::DownArrow);
    m_optionsToggle->setText(expanded ? QStringLiteral("Ocultar opciones")
                                     : QStringLiteral("Mostrar opciones"));
    resize(expanded ? QSize(680, 640) : QSize(430, 285));
}

void ConnectionDialog::updateQuickUserLabel()
{
    if (!m_quickUserLabel)
        return;

    const QString user = m_userEdit ? m_userEdit->text().trimmed() : QString();
    const QString domain = m_domainEdit ? m_domainEdit->text().trimmed() : QString();
    QString value = user;
    if (!domain.isEmpty())
        value = domain + QLatin1Char('\\') + user;
    if (value.isEmpty())
        value = QStringLiteral("Ninguno especificado");
    m_quickUserLabel->setText(QStringLiteral("Usuario:    %1").arg(value));
}

void ConnectionDialog::saveAsNewProfile()
{
    collectForm();
    m_profile.id.clear();
    m_profile.name = m_profile.name.isEmpty()
                         ? m_profile.displayName() + QStringLiteral(" (copia)")
                         : m_profile.name + QStringLiteral(" (copia)");
    // Las credenciales se mantienen separadas del perfil y no se duplican.
    m_profile.rememberPassword = false;
    if (!m_store->add(m_profile))
    {
        showError(QStringLiteral("No se pudo guardar el perfil nuevo."));
        return;
    }

    const QList<ConnectionProfile> profiles = m_store->profiles();
    refreshProfileList(profiles.last().id);
}

void ConnectionDialog::onNewProfile()
{
    ConnectionProfile profile;
    profile.name = QStringLiteral("Nuevo perfil");
    if (!m_store->add(profile))
    {
        showError(QStringLiteral("No se pudo crear el perfil."));
        return;
    }

    refreshProfileList(m_store->profiles().last().id);
    m_profileNameEdit->setFocus();
    m_profileNameEdit->selectAll();
}

void ConnectionDialog::onDuplicateProfile()
{
    if (m_selectedId.isEmpty())
        return;

    ConnectionProfile copy = m_store->profile(m_selectedId);
    copy.id.clear();
    copy.name = copy.name.isEmpty() ? copy.displayName()
				    : QStringLiteral("%1 (copia)").arg(copy.name);
    if (!m_store->add(copy))
    {
        showError(QStringLiteral("No se pudo duplicar el perfil."));
        return;
    }

    refreshProfileList(m_store->profiles().last().id);
}

void ConnectionDialog::onDeleteProfile()
{
    if (m_selectedId.isEmpty())
        return;

    const ConnectionProfile profile = m_store->profile(m_selectedId);
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Eliminar perfil"),
        QStringLiteral("¿Eliminar el perfil «%1»?").arg(profile.displayName()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    if (profile.rememberPassword)
    {
        CredentialStore* store = CredentialStore::instance();
        store->remove(CredentialStore::credentialId(profile));
    }

    const int index = m_store->indexOf(profile.id);
    if (!m_store->remove(profile.id))
    {
        showError(QStringLiteral("No se pudo eliminar el perfil."));
        return;
    }

    const int next = (index >= 0 && index < m_store->count()) ? index : m_store->count() - 1;
    refreshProfileList(next >= 0 ? m_store->profiles().at(next).id : QString());
}

void ConnectionDialog::onMoveProfile(int offset)
{
    if (m_selectedId.isEmpty() || !m_store->move(m_selectedId, offset))
        return;

    const int index = m_store->indexOf(m_selectedId);
    refreshProfileList(m_selectedId);
    if (m_profileList->count() > index)
        m_profileList->setCurrentRow(index);
    updateActionButtons();
}

void ConnectionDialog::onImportRdp()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Importar archivo .rdp"),
        QDir::homePath(), QStringLiteral("Archivos RDP (*.rdp);;Todos los archivos (*)"));
    if (path.isEmpty())
        return;

    if (!m_store->importRdpFile(path))
    {
        showError(QStringLiteral("No se pudo importar «%1».").arg(QFileInfo(path).fileName()));
        return;
    }

    const QString name = QFileInfo(path).completeBaseName();
    const QList<ConnectionProfile> profiles = m_store->profiles();
    for (int index = profiles.size() - 1; index >= 0; --index)
    {
        if (profiles.at(index).name == name)
	{
	    refreshProfileList(profiles.at(index).id);
	    return;
	}
    }

    refreshProfileList();
}

void ConnectionDialog::onExportRdp()
{
    if (m_selectedId.isEmpty())
        return;

    collectForm();

    QString suggested = QDir::homePath() + QLatin1Char('/') +
			(m_profile.name.isEmpty() ? QStringLiteral("escritorio.rdp")
						 : m_profile.name + QStringLiteral(".rdp"));
    QString path = QFileDialog::getSaveFileName(this,
						QStringLiteral("Exportar archivo .rdp"),
						suggested,
						QStringLiteral("Archivos RDP (*.rdp)"));
    if (path.isEmpty())
        return;

    if (path.endsWith(QLatin1Char('/')))
        path += QStringLiteral("escritorio.rdp");
    if (!path.endsWith(QLatin1String(".rdp"), Qt::CaseInsensitive))
        path += QStringLiteral(".rdp");

    if (!RdpFile::write(path, m_profile))
        showError(QStringLiteral("No se pudo exportar el perfil."));
}

void ConnectionDialog::updateOkButton()
{
    if (m_buttonBox)
    {
        auto* ok = m_buttonBox->button(QDialogButtonBox::Ok);
        if (ok)
            ok->setEnabled(!m_hostEdit->text().trimmed().isEmpty());
    }

    if (!m_populating && m_errorLabel)
        m_errorLabel->setVisible(false);
}

QString ConnectionDialog::targetCredentialId() const
{
    ConnectionProfile target = m_profile;
    target.host = m_hostEdit ? m_hostEdit->text().trimmed() : target.host;
    target.port = m_portSpin ? static_cast<quint16>(m_portSpin->value()) : target.port;
    target.username = m_userEdit ? m_userEdit->text().trimmed() : target.username;
    target.domain = m_domainEdit ? m_domainEdit->text().trimmed() : target.domain;
    return CredentialStore::credentialId(target);
}

void ConnectionDialog::updatePasswordHint()
{
    if (!m_passwordHint || !m_rememberPasswordCheck)
        return;

    if (!m_rememberPasswordCheck->isChecked())
    {
        if (!m_passwordEdit->text().isEmpty())
        {
            m_passwordHint->setText(
		QStringLiteral("Si no quieres escribirla cada vez, activa "
			       "\"Recordar contraseña\"."));
        }
        else
        {
            m_passwordHint->setText(QStringLiteral("No se guardará la contraseña."));
        }
        return;
    }

    CredentialStore* store = CredentialStore::instance();
    const bool hasStored = !store->read(targetCredentialId()).isEmpty();

    if (hasStored)
    {
        m_passwordHint->setText(
	    QStringLiteral("Contraseña guardada en %1.").arg(store->backendName()));
    }
    else
    {
        m_passwordHint->setText(
	    QStringLiteral("Se guardará en %1.").arg(store->backendName()));
    }
}

void ConnectionDialog::updateActionButtons()
{
    if (!m_store)
        return;

    const int index = m_store->indexOf(m_selectedId);
    const bool hasSelection = index >= 0;

    if (m_duplicateButton)
        m_duplicateButton->setEnabled(hasSelection);
    if (m_deleteButton)
        m_deleteButton->setEnabled(hasSelection);
    if (m_exportButton)
        m_exportButton->setEnabled(hasSelection);
    if (m_upButton)
        m_upButton->setEnabled(hasSelection && (index > 0));
    if (m_downButton)
        m_downButton->setEnabled(hasSelection && (index >= 0) &&
				 (index < m_store->count() - 1));
}

void ConnectionDialog::showError(const QString& message)
{
    if (!m_errorLabel)
        return;
    m_errorLabel->setText(message);
    m_errorLabel->setVisible(true);
}

void ConnectionDialog::persistProfile()
{
    collectForm();

    if (!m_selectedId.isEmpty())
    {
        m_profile.id = m_selectedId;
        if (!m_store->update(m_profile))
            showError(QStringLiteral("No se pudo guardar el perfil."));
        else if (QListWidgetItem* item = m_profileList->currentItem())
            item->setText(m_profile.name);
        return;
    }

    if (!m_store->add(m_profile))
    {
        showError(QStringLiteral("No se pudo guardar el perfil."));
        return;
    }

    m_profile = m_store->profiles().last();
    m_selectedId = m_profile.id;
    refreshProfileList(m_selectedId);
}

bool ConnectionDialog::persistCredentials(QString* error)
{
    CredentialStore* store = CredentialStore::instance();
    const QString credentialId = targetCredentialId();

    if (!m_rememberPasswordCheck->isChecked())
    {
        // Desmarcar la casilla borra la contraseña guardada.
        return store->remove(credentialId, error);
    }

    // Con la casilla activa y el campo vacío se conserva la que ya existe:
    // significa «usa la que guardé».
    if (m_passwordEdit->text().isEmpty())
        return true;

    return store->write(credentialId, m_passwordEdit->text(), error);
}

void ConnectionDialog::accept()
{
    collectForm();

    if (m_profile.host.isEmpty())
    {
        showError(QStringLiteral("Escribe el nombre del equipo o su dirección IP."));
        return;
    }

    m_passwordHint->setVisible(false);
    m_errorLabel->setVisible(false);

    // Guardar un perfil nuevo refresca la lista y carga de nuevo el formulario,
    // lo cual limpia por seguridad el campo de contraseña. Conservamos el valor
    // local durante ese refresco para escribirlo en CredentialStore y devolverlo
    // después a main() como credencial de esta conexión.
    const QString enteredPassword = m_passwordEdit->text();
    persistProfile();
    if (!enteredPassword.isEmpty())
        m_passwordEdit->setText(enteredPassword);

    QString credentialError;
    if (!persistCredentials(&credentialError))
    {
        showError(QStringLiteral("No se pudo guardar la contraseña: %1. "
                                 "Comprueba el almacén de contraseñas o desmarca "
                                 "«Recordar contraseña».")
                      .arg(credentialError));
        return;
    }

    if (!m_profile.id.isEmpty())
    {
        QSettings settings(settingsFilePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("app/lastProfileId"), m_profile.id);
        settings.sync();
    }

    QDialog::accept();
}

void ConnectionDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // QDialogButtonBox restablece los rótulos estándar al pulir el diálogo;
    // aplicar los textos después de mostrarlo conserva la terminología local.
    if (m_buttonBox)
    {
        m_buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Conectar"));
        m_buttonBox->button(QDialogButtonBox::Help)->setText(QStringLiteral("Ayuda"));
    }
    QTimer::singleShot(0, this, [this]()
    {
        if (m_buttonBox)
        {
            m_buttonBox->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Conectar"));
            m_buttonBox->button(QDialogButtonBox::Help)->setText(QStringLiteral("Ayuda"));
        }
    });
}

qtrdp::ConnectionProfile ConnectionDialog::profile() const
{
    return m_profile;
}

qtrdp::SessionBackend::Credentials ConnectionDialog::credentials() const
{
    SessionBackend::Credentials result;
    result.username = m_userEdit->text().trimmed();
    result.domain = m_domainEdit->text().trimmed();

    if (m_rememberPasswordCheck->isChecked() && m_passwordEdit->text().isEmpty())
	result.password = CredentialStore::instance()->read(targetCredentialId());
    else
	result.password = m_passwordEdit->text();

    return result;
}

bool ConnectionDialog::promptForCredentials(QWidget* parent, ConnectionProfile& profile,
                                             SessionBackend::Credentials& credentials,
                                             bool& rememberPassword, const QString& reason)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Conexión a Escritorio remoto"));
    dialog.setWindowIcon(QIcon(QStringLiteral(":/icons/qtrdp.svg")));

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 10);
    layout->setSpacing(8);

    auto* title = new QLabel(QStringLiteral("Escriba sus credenciales"), &dialog);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* description = new QLabel(
        reason.isEmpty() ? QStringLiteral("Se necesitan credenciales para conectarse al equipo remoto.")
                         : reason,
        &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* user = new QLineEdit(&dialog);
    user->setText(credentials.username.isEmpty() ? profile.username : credentials.username);
    user->setObjectName(QStringLiteral("promptUserEdit"));
    auto* domain = new QLineEdit(&dialog);
    domain->setText(credentials.domain.isEmpty() ? profile.domain : credentials.domain);
    domain->setObjectName(QStringLiteral("promptDomainEdit"));
    domain->setPlaceholderText(QStringLiteral("opcional"));
    auto* password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    password->setObjectName(QStringLiteral("promptPasswordEdit"));
    password->setFocus();
    auto* remember = new QCheckBox(QStringLiteral("Recordar contraseña"), &dialog);
    remember->setChecked(rememberPassword || profile.rememberPassword);
    form->addRow(QStringLiteral("Usuario:"), user);
    form->addRow(QStringLiteral("Dominio:"), domain);
    form->addRow(QStringLiteral("Contraseña:"), password);
    form->addRow(QString(), remember);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Conectar"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(password, &QLineEdit::returnPressed, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    profile.username = user->text().trimmed();
    profile.domain = domain->text().trimmed();
    credentials.username = profile.username;
    credentials.domain = profile.domain;
    credentials.password = password->text();
    rememberPassword = remember->isChecked();
    profile.rememberPassword = rememberPassword;
    return true;
}

} // namespace qtrdp
