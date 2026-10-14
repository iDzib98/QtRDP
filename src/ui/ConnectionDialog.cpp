#include "ui/ConnectionDialog.h"

#include "core/CredentialStore.h"
#include "core/RdpFile.h"
#include "core/XFreeRdpLauncher.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDialog>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPair>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
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

// Ajusta el texto a un ancho dado insertando saltos de línea en los espacios,
// para que las casillas con rótulos largos no fuercen el scroll horizontal.
QString wrapToWidth(const QString& text, const QFontMetrics& metrics, int width)
{
    if (width <= 0 || text.isEmpty())
        return text;

    QString result;
    QString line;
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& word : words)
    {
        const QString candidate = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
        if (!line.isEmpty() && metrics.horizontalAdvance(candidate) > width)
        {
            result += line + QLatin1Char('\n');
            line = word;
        }
        else
        {
            line = candidate;
        }
    }
    result += line;
    return result;
}

// QCheckBox no ajusta el texto por sí solo; esta subclase lo reparte en varias
// líneas y acota el ancho solicitado, de modo que un rótulo largo no fuerce el
// scroll horizontal del diálogo.
class WrappingCheckBox : public QCheckBox
{
public:
    explicit WrappingCheckBox(const QString& text, QWidget* parent = nullptr)
        : QCheckBox(parent), m_fullText(text)
    {
        QCheckBox::setText(text);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(fontMetrics());
        const int indicator = indicatorWidth();
        const int textWidth =
            qMax(80, (m_wrapWidth > 0 ? m_wrapWidth : kPreferredWrapWidth) - indicator);
        const QRect bounds =
            fm.boundingRect(QRect(0, 0, textWidth, 0), Qt::TextWordWrap, m_fullText);
        return QSize(bounds.width() + indicator,
                     qMax(QCheckBox::sizeHint().height(), bounds.height()));
    }

    QSize minimumSizeHint() const override
    {
        QSize hint = QCheckBox::minimumSizeHint();
        hint.setWidth(qMin(hint.width(), indicatorWidth() + 80));
        return hint;
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QCheckBox::resizeEvent(event);
        rewrap();
    }

    void changeEvent(QEvent* event) override
    {
        QCheckBox::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        {
            m_wrapWidth = -1;
            rewrap();
        }
    }

private:
    int indicatorWidth() const
    {
        return style()->pixelMetric(QStyle::PM_IndicatorWidth) +
               style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing);
    }

    void rewrap()
    {
        const int available = width() - indicatorWidth();
        if (available <= 0 || available == m_wrapWidth)
            return;
        m_wrapWidth = available;
        QCheckBox::setText(wrapToWidth(m_fullText, fontMetrics(), available));
        updateGeometry();
    }

    static constexpr int kPreferredWrapWidth = 520;
    QString m_fullText;
    int m_wrapWidth = -1;
};

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
    const auto addScrollableTab = [this](QWidget* content, const QString& title)
    {
        auto* area = new QScrollArea(m_tabs);
        area->setWidget(content);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        m_tabs->addTab(area, title);
    };
    addScrollableTab(buildForm(), QStringLiteral("General"));
    addScrollableTab(buildScreenPage(), QStringLiteral("Pantalla"));
    addScrollableTab(buildLocalResourcesPage(), QStringLiteral("Recursos locales"));
    addScrollableTab(buildPerformancePage(), QStringLiteral("Rendimiento"));
    addScrollableTab(buildAdvancedPage(), QStringLiteral("Opciones avanzadas"));

    // El «modo desarrollo» muestra el comando de xfreerdp resultante; estos
    // enlaces lo mantienen actualizado ante cualquier cambio del formulario.
    // Se usa un temporizador para no reconstruir el comando en cada pulsación.
    m_commandPreviewTimer = new QTimer(this);
    m_commandPreviewTimer->setSingleShot(true);
    m_commandPreviewTimer->setInterval(150);
    connect(m_commandPreviewTimer, &QTimer::timeout, this,
            &ConnectionDialog::updateCommandPreview);
    const auto schedulePreview = [this]() { m_commandPreviewTimer->start(); };

    for (QCheckBox* box : m_tabs->findChildren<QCheckBox*>())
        connect(box, &QCheckBox::toggled, this, schedulePreview);
    for (QComboBox* combo : m_tabs->findChildren<QComboBox*>())
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, schedulePreview);
    for (QLineEdit* edit : m_tabs->findChildren<QLineEdit*>())
        connect(edit, &QLineEdit::textChanged, this, schedulePreview);
    for (QSpinBox* spin : m_tabs->findChildren<QSpinBox*>())
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, schedulePreview);

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
    addFormRowWithHint(form, QStringLiteral("Equipo:"), m_hostEdit,
                       QStringLiteral("/v:<equipo>[:<puerto>]"));

    auto* portRow = new QHBoxLayout;
    m_portSpin = new QSpinBox(this);
    m_portSpin->setObjectName(QStringLiteral("portSpin"));
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(3389);
    portRow->addWidget(m_portSpin);
    portRow->addStretch(1);
    form->addRow(QStringLiteral("Puerto:"), portRow);
    addHintRow(form, m_portSpin, QStringLiteral("/v:<equipo>:<puerto>"));

    m_userEdit = new QLineEdit(this);
    m_userEdit->setObjectName(QStringLiteral("userEdit"));
    m_userEdit->setPlaceholderText(QStringLiteral("usuario"));
    addFormRowWithHint(form, QStringLiteral("Usuario:"), m_userEdit,
                       QStringLiteral("/u:<usuario>"));

    m_domainEdit = new QLineEdit(this);
    m_domainEdit->setObjectName(QStringLiteral("domainEdit"));
    m_domainEdit->setPlaceholderText(QStringLiteral("opcional — dejar vacío si no hay dominio"));
    addFormRowWithHint(form, QStringLiteral("Dominio:"), m_domainEdit,
                       QStringLiteral("/d:<dominio>"));

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setObjectName(QStringLiteral("passwordEdit"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("contraseña"));
    addFormRowWithHint(form, QStringLiteral("Contraseña:"), m_passwordEdit,
                       QStringLiteral("/p:<contraseña>"));

    m_passwordHint = new QLabel(this);
    m_passwordHint->setObjectName(QStringLiteral("passwordHint"));
    m_passwordHint->setStyleSheet(QStringLiteral("color: #666;"));
    m_passwordHint->setWordWrap(true);
    form->addRow(QString(), m_passwordHint);

    m_rememberUserCheck = new WrappingCheckBox(QStringLiteral("Recordar mi usuario"), this);
    m_rememberUserCheck->setChecked(true);
    addFormRowWithHint(form, QString(), m_rememberUserCheck,
                       QStringLiteral("(Solo local: no envía parámetros a xfreerdp)"));

    m_rememberPasswordCheck = new WrappingCheckBox(QStringLiteral("Recordar contraseña"), this);
    m_rememberPasswordCheck->setObjectName(QStringLiteral("rememberPasswordCheck"));
    addFormRowWithHint(form, QString(), m_rememberPasswordCheck,
                       QStringLiteral("(Solo local: no envía parámetros a xfreerdp)"));

    auto* accountGroup = new QGroupBox(QStringLiteral("Configuración de la conexión"), page);
    auto* accountLayout = new QVBoxLayout(accountGroup);
    accountLayout->setContentsMargins(10, 16, 10, 8);
    accountLayout->setSpacing(5);
    auto* profileNameRow = new QFormLayout;
    addFormRowWithHint(profileNameRow, QStringLiteral("Nombre del perfil:"), m_profileNameEdit,
                       QStringLiteral("/title:<nombre>"));
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
    addHintRow(screenForm, m_resolutionCombo,
               QStringLiteral("/size:<ancho>x<alto> · /f · /dynamic-resolution"));

    m_colorDepthCombo = new QComboBox(screenGroup);
    m_colorDepthCombo->setObjectName(QStringLiteral("colorDepthCombo"));
    m_colorDepthCombo->addItem(QStringLiteral("Calidad más alta (32 bits)"), 32);
    m_colorDepthCombo->addItem(QStringLiteral("Alta calidad (24 bits)"), 24);
    m_colorDepthCombo->addItem(QStringLiteral("Media (16 bits)"), 16);
    m_colorDepthCombo->addItem(QStringLiteral("Baja (8 bits)"), 8);
    addFormRowWithHint(screenForm, QStringLiteral("Colores:"), m_colorDepthCombo,
                       QStringLiteral("/bpp:<bits>"));

    m_dynamicResolutionCheck = new WrappingCheckBox(
        QStringLiteral("Cambiar el tamaño del escritorio remoto al cambiar el tamaño de la ventana"),
        screenGroup);
    m_dynamicResolutionCheck->setObjectName(QStringLiteral("dynamicResolutionCheck"));
    addFormRowWithHint(screenForm, QString(), m_dynamicResolutionCheck,
                       QStringLiteral("/dynamic-resolution"));
    m_multimonCheck = new WrappingCheckBox(
        QStringLiteral("Usar todos mis monitores para la sesión remota"), screenGroup);
    addFormRowWithHint(screenForm, QString(), m_multimonCheck, QStringLiteral("/multimon"));
    m_connectionBarCheck = new WrappingCheckBox(
        QStringLiteral("Mostrar la barra de conexión cuando se use pantalla completa"),
        screenGroup);
    addFormRowWithHint(
        screenForm, QString(), m_connectionBarCheck,
        QStringLiteral("/floatbar:sticky:on,default:visible,show:always"));

    m_scaleCombo = new QComboBox(screenGroup);
    m_scaleCombo->setObjectName(QStringLiteral("scaleCombo"));
    m_scaleCombo->addItem(QStringLiteral("Sin escala (100 %)"), 100);
    m_scaleCombo->addItem(QStringLiteral("Escala del 140 %"), 140);
    m_scaleCombo->addItem(QStringLiteral("Escala del 180 %"), 180);
    addFormRowWithHint(screenForm, QStringLiteral("Escala del escritorio:"), m_scaleCombo,
                       QStringLiteral("/scale:<porcentaje>"));

    m_orientationCombo = new QComboBox(screenGroup);
    m_orientationCombo->setObjectName(QStringLiteral("orientationCombo"));
    m_orientationCombo->addItem(QStringLiteral("Normal (0°)"), 0);
    m_orientationCombo->addItem(QStringLiteral("90°"), 90);
    m_orientationCombo->addItem(QStringLiteral("180°"), 180);
    m_orientationCombo->addItem(QStringLiteral("270°"), 270);
    addFormRowWithHint(screenForm, QStringLiteral("Orientación de la pantalla:"), m_orientationCombo,
                       QStringLiteral("/orientation:<grados>"));

    m_spanMonitorsCheck = new WrappingCheckBox(
        QStringLiteral("Expandir la sesión por todos los monitores (span)"), screenGroup);
    m_spanMonitorsCheck->setObjectName(QStringLiteral("spanMonitorsCheck"));
    addFormRowWithHint(screenForm, QString(), m_spanMonitorsCheck, QStringLiteral("+span"));
    m_smartSizingCheck = new WrappingCheckBox(
        QStringLiteral("Ajustar el escritorio remoto al tamaño de la ventana"), screenGroup);
    m_smartSizingCheck->setObjectName(QStringLiteral("smartSizingCheck"));
    addFormRowWithHint(screenForm, QString(), m_smartSizingCheck,
                       QStringLiteral("+smart-sizing"));

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
    m_audioCheck = new WrappingCheckBox(QStringLiteral("Reproducir el audio en este equipo"), audioGroup);
    m_audioCheck->setObjectName(QStringLiteral("redirectAudioCheck"));
    m_microphoneCheck = new WrappingCheckBox(QStringLiteral("Grabar desde este equipo"), audioGroup);
    addWidgetWithHint(audioLayout, m_audioCheck, QStringLiteral("/sound"));
    addWidgetWithHint(audioLayout, m_microphoneCheck, QStringLiteral("/microphone"));
    layout->addWidget(audioGroup);

    auto* keyboardGroup = new QGroupBox(QStringLiteral("Teclado"), page);
    auto* keyboardForm = new QFormLayout(keyboardGroup);
    m_keyboardCombo = new QComboBox(keyboardGroup);
    m_keyboardCombo->setObjectName(QStringLiteral("keyboardCombo"));
    m_keyboardCombo->addItem(QStringLiteral("Solo cuando se use la pantalla completa"),
                             QStringLiteral("fullscreen"));
    m_keyboardCombo->addItem(QStringLiteral("En este equipo"), QStringLiteral("local"));
    m_keyboardCombo->addItem(QStringLiteral("En el equipo remoto"), QStringLiteral("remote"));
    addFormRowWithHint(keyboardForm,
                       QStringLiteral("Aplicar combinaciones de teclas de Windows:"),
                       m_keyboardCombo, QStringLiteral("-grab-keyboard (solo «En el equipo remoto»)"));
    layout->addWidget(keyboardGroup);

    auto* devicesGroup = new QGroupBox(QStringLiteral("Dispositivos y recursos locales"), page);
    auto* devicesLayout = new QVBoxLayout(devicesGroup);
    m_clipboardCheck = new WrappingCheckBox(QStringLiteral("Portapapeles"), devicesGroup);
    m_clipboardFilesCheck =
        new WrappingCheckBox(QStringLiteral("Permitir copiar archivos por el portapapeles"), devicesGroup);
    m_clipboardFilesCheck->setObjectName(QStringLiteral("clipboardFilesCheck"));
    m_drivesCheck = new WrappingCheckBox(QStringLiteral("Unidades"), devicesGroup);
    m_printersCheck = new WrappingCheckBox(QStringLiteral("Impresoras"), devicesGroup);
    m_homeDriveCheck = new WrappingCheckBox(QStringLiteral("Compartir mi carpeta personal"), devicesGroup);
    m_redirectPortsCheck = new WrappingCheckBox(QStringLiteral("Puertos"), devicesGroup);
    m_redirectSmartCardsCheck = new WrappingCheckBox(QStringLiteral("Tarjetas inteligentes"), devicesGroup);
    m_multitouchCheck = new WrappingCheckBox(QStringLiteral("Entrada multitáctil"), devicesGroup);
    m_multitouchCheck->setObjectName(QStringLiteral("multitouchCheck"));
    addWidgetWithHint(devicesLayout, m_clipboardCheck, QStringLiteral("/clipboard"));
    addWidgetWithHint(devicesLayout, m_clipboardFilesCheck,
                      QStringLiteral("/clipboard:files-to:off (al desmarcarla)"));
    addWidgetWithHint(devicesLayout, m_drivesCheck, QStringLiteral("/drive:<nombre>,<ruta>"));
    auto* drivesForm = new QFormLayout;
    m_drivesEdit = new QLineEdit(devicesGroup);
    m_drivesEdit->setObjectName(QStringLiteral("drivesEdit"));
    m_drivesEdit->setPlaceholderText(QStringLiteral("nombre,ruta; por ejemplo: home,/home/usuario"));
    drivesForm->addRow(QStringLiteral("Unidades específicas:"), m_drivesEdit);
    addHintRow(drivesForm, m_drivesEdit, QStringLiteral("/drive:<nombre>,<ruta>"));
    devicesLayout->addLayout(drivesForm);
    addWidgetWithHint(devicesLayout, m_homeDriveCheck,
                      QStringLiteral("/drive:home,<carpeta personal>"));
    addWidgetWithHint(devicesLayout, m_printersCheck, QStringLiteral("/printer"));
    addWidgetWithHint(devicesLayout, m_redirectPortsCheck,
                      QStringLiteral("(Sin parámetro en xfreerdp)"));
    addWidgetWithHint(devicesLayout, m_redirectSmartCardsCheck, QStringLiteral("/smartcard"));
    addWidgetWithHint(devicesLayout, m_multitouchCheck, QStringLiteral("+multitouch"));
    layout->addWidget(devicesGroup, 1);
    connect(m_clipboardCheck, &QCheckBox::toggled, m_clipboardFilesCheck,
            &QCheckBox::setEnabled);
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
    addFormRowWithHint(networkForm, QStringLiteral("Velocidad de conexión:"), m_networkCombo,
                       QStringLiteral("/network:auto|modem|broadband|lan"));
    performanceLayout->addLayout(networkForm);

    m_wallpaperCheck = new WrappingCheckBox(QStringLiteral("Fondo de escritorio"), performanceGroup);
    m_fontSmoothingCheck = new WrappingCheckBox(QStringLiteral("Suavizado de fuentes"), performanceGroup);
    m_desktopCompositionCheck = new WrappingCheckBox(QStringLiteral("Composición del escritorio"),
                                               performanceGroup);
    m_animationsCheck = new WrappingCheckBox(QStringLiteral("Animaciones de menús y ventanas"),
                                       performanceGroup);
    m_bitmapCacheCheck = new WrappingCheckBox(
        QStringLiteral("Almacenamiento en caché persistente de mapas de bits"), performanceGroup);
    m_reconnectCheck = new WrappingCheckBox(QStringLiteral("Volver a conectarse si se pierde la conexión"),
                                      performanceGroup);
    m_windowDragCheck = new WrappingCheckBox(
        QStringLiteral("Mostrar el contenido de la ventana al arrastrarla"), performanceGroup);
    m_windowDragCheck->setObjectName(QStringLiteral("windowDragCheck"));
    addWidgetWithHint(performanceLayout, m_wallpaperCheck,
                      QStringLiteral("+wallpaper / -wallpaper"));
    addWidgetWithHint(performanceLayout, m_fontSmoothingCheck, QStringLiteral("+fonts / -fonts"));
    addWidgetWithHint(performanceLayout, m_desktopCompositionCheck,
                      QStringLiteral("+aero / -aero"));
    addWidgetWithHint(performanceLayout, m_animationsCheck,
                      QStringLiteral("+menu-anims / -menu-anims"));
    addWidgetWithHint(performanceLayout, m_windowDragCheck, QStringLiteral("+window-drag"));
    addWidgetWithHint(performanceLayout, m_bitmapCacheCheck,
                      QStringLiteral("/cache:bitmap:on / off"));
    addWidgetWithHint(performanceLayout, m_reconnectCheck,
                      QStringLiteral("+auto-reconnect / -auto-reconnect"));

    auto* codecForm = new QFormLayout;
    m_compressionCombo = new QComboBox(performanceGroup);
    m_compressionCombo->setObjectName(QStringLiteral("compressionCombo"));
    m_compressionCombo->addItem(QStringLiteral("Predeterminada"), -1);
    m_compressionCombo->addItem(QStringLiteral("Baja (0)"), 0);
    m_compressionCombo->addItem(QStringLiteral("Media (1)"), 1);
    m_compressionCombo->addItem(QStringLiteral("Alta (2)"), 2);
    addFormRowWithHint(codecForm, QStringLiteral("Nivel de compresión:"), m_compressionCombo,
                       QStringLiteral("/compression-level:<0-2>"));
    m_gdiCombo = new QComboBox(performanceGroup);
    m_gdiCombo->setObjectName(QStringLiteral("gdiCombo"));
    m_gdiCombo->addItem(QStringLiteral("Predeterminado"), QStringLiteral("default"));
    m_gdiCombo->addItem(QStringLiteral("Software (sw)"), QStringLiteral("sw"));
    m_gdiCombo->addItem(QStringLiteral("Hardware (hw)"), QStringLiteral("hw"));
    addFormRowWithHint(codecForm, QStringLiteral("Representación GDI:"), m_gdiCombo,
                       QStringLiteral("/gdi:sw|hw"));
    performanceLayout->addLayout(codecForm);

    m_rfxCheck = new WrappingCheckBox(QStringLiteral("Habilitar RemoteFX (+rfx)"), performanceGroup);
    m_nscCheck = new WrappingCheckBox(QStringLiteral("Habilitar NSCodec (+nsc)"), performanceGroup);
    m_jpegCheck = new WrappingCheckBox(QStringLiteral("Habilitar códec JPEG (+jpeg)"), performanceGroup);
    m_videoCheck = new WrappingCheckBox(
        QStringLiteral("Habilitar vídeo optimizado (+video)"), performanceGroup);
    addWidgetWithHint(performanceLayout, m_rfxCheck, QStringLiteral("+rfx"));
    addWidgetWithHint(performanceLayout, m_nscCheck, QStringLiteral("+nsc"));
    addWidgetWithHint(performanceLayout, m_jpegCheck, QStringLiteral("+jpeg"));
    addWidgetWithHint(performanceLayout, m_videoCheck, QStringLiteral("+video"));
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
    addHintRow(authLayout, m_authWarningCombo, QStringLiteral("/cert:deny|tofu|ignore"));
    m_secProtocolCombo = new QComboBox(authGroup);
    m_secProtocolCombo->setObjectName(QStringLiteral("secProtocolCombo"));
    m_secProtocolCombo->addItem(QStringLiteral("Negociar automáticamente"), QStringLiteral("default"));
    m_secProtocolCombo->addItem(QStringLiteral("Seguridad RDP (/sec:rdp)"), QStringLiteral("rdp"));
    m_secProtocolCombo->addItem(QStringLiteral("TLS (/sec:tls)"), QStringLiteral("tls"));
    m_secProtocolCombo->addItem(QStringLiteral("NLA (/sec:nla)"), QStringLiteral("nla"));
    m_secProtocolCombo->addItem(QStringLiteral("Extended (/sec:ext)"), QStringLiteral("ext"));
    authLayout->addRow(QStringLiteral("Protocolo de seguridad:"), m_secProtocolCombo);
    addHintRow(authLayout, m_secProtocolCombo, QStringLiteral("/sec:rdp|tls|nla|ext"));
    layout->addWidget(authGroup);

    auto* gatewayGroup = new QGroupBox(QStringLiteral("Conectarse desde cualquier lugar"), page);
    auto* gatewayLayout = new QFormLayout(gatewayGroup);
    m_gatewayCheck = new WrappingCheckBox(
        QStringLiteral("Usar una puerta de enlace de Escritorio remoto"), gatewayGroup);
    addFormRowWithHint(gatewayLayout, QString(), m_gatewayCheck,
                       QStringLiteral("/g:<servidor> y /gp:<puerto>"));
    m_gatewayHostEdit = new QLineEdit(gatewayGroup);
    m_gatewayHostEdit->setObjectName(QStringLiteral("gatewayHostEdit"));
    addFormRowWithHint(gatewayLayout, QStringLiteral("Servidor de puerta de enlace:"), m_gatewayHostEdit,
                       QStringLiteral("/g:<servidor>"));
    m_gatewayPortSpin = new QSpinBox(gatewayGroup);
    m_gatewayPortSpin->setObjectName(QStringLiteral("gatewayPortSpin"));
    m_gatewayPortSpin->setRange(1, 65535);
    m_gatewayPortSpin->setValue(443);
    addFormRowWithHint(gatewayLayout, QStringLiteral("Puerto:"), m_gatewayPortSpin,
                       QStringLiteral("/gp:<puerto>"));
    m_gatewaySameCredentialsCheck = new WrappingCheckBox(
        QStringLiteral("Usar las mismas credenciales para la puerta de enlace"), gatewayGroup);
    addFormRowWithHint(gatewayLayout, QString(), m_gatewaySameCredentialsCheck,
                       QStringLiteral("(Omite /gu: reutiliza las credenciales)"));
    m_gatewayUserEdit = new QLineEdit(gatewayGroup);
    m_gatewayUserEdit->setObjectName(QStringLiteral("gatewayUserEdit"));
    addFormRowWithHint(gatewayLayout, QStringLiteral("Usuario de puerta de enlace:"), m_gatewayUserEdit,
                       QStringLiteral("/gu:<usuario>"));
    layout->addWidget(gatewayGroup);

    auto* appGroup = new QGroupBox(QStringLiteral("Autenticación del usuario"), page);
    auto* appLayout = new QFormLayout(appGroup);
    m_webAccountCheck = new WrappingCheckBox(
        QStringLiteral("Usar una cuenta web para iniciar sesión en el equipo remoto"), appGroup);
    addFormRowWithHint(appLayout, QString(), m_webAccountCheck,
                       QStringLiteral("(Sin parámetro en xfreerdp)"));
    m_remoteAppCheck = new WrappingCheckBox(QStringLiteral("Iniciar una aplicación remota"), appGroup);
    addFormRowWithHint(appLayout, QString(), m_remoteAppCheck, QStringLiteral("/app:<programa>"));
    m_remoteProgramEdit = new QLineEdit(appGroup);
    m_remoteProgramEdit->setObjectName(QStringLiteral("remoteProgramEdit"));
    addFormRowWithHint(appLayout, QStringLiteral("Programa remoto:"), m_remoteProgramEdit,
                       QStringLiteral("/app:<programa>"));
    layout->addWidget(appGroup);

    auto* sessionGroup = new QGroupBox(QStringLiteral("Sesión y conexión"), page);
    auto* sessionForm = new QFormLayout(sessionGroup);
    m_adminCheck = new WrappingCheckBox(QStringLiteral("Conectarse a la consola (sesión de administración)"),
                                 sessionGroup);
    addFormRowWithHint(sessionForm, QString(), m_adminCheck, QStringLiteral("+admin"));
    m_restrictedAdminCheck =
        new WrappingCheckBox(QStringLiteral("Modo de administración restringida"), sessionGroup);
    addFormRowWithHint(sessionForm, QString(), m_restrictedAdminCheck,
                       QStringLiteral("+restricted-admin"));
    m_workareaCheck =
        new WrappingCheckBox(QStringLiteral("Usar solo el área de trabajo disponible"), sessionGroup);
    addFormRowWithHint(sessionForm, QString(), m_workareaCheck, QStringLiteral("+workarea"));
    m_shellEdit = new QLineEdit(sessionGroup);
    m_shellEdit->setObjectName(QStringLiteral("shellEdit"));
    m_shellEdit->setPlaceholderText(QStringLiteral("por ejemplo: cmd.exe"));
    addFormRowWithHint(sessionForm, QStringLiteral("Shell alternativo:"), m_shellEdit,
                       QStringLiteral("/shell:<comando>"));
    m_shellDirEdit = new QLineEdit(sessionGroup);
    m_shellDirEdit->setObjectName(QStringLiteral("shellDirEdit"));
    addFormRowWithHint(sessionForm, QStringLiteral("Directorio de trabajo:"), m_shellDirEdit,
                       QStringLiteral("/shell-dir:<directorio>"));
    m_clientHostnameEdit = new QLineEdit(sessionGroup);
    m_clientHostnameEdit->setObjectName(QStringLiteral("clientHostnameEdit"));
    addFormRowWithHint(sessionForm, QStringLiteral("Nombre de equipo del cliente:"), m_clientHostnameEdit,
                       QStringLiteral("/client-hostname:<nombre>"));
    m_timeoutSpin = new QSpinBox(sessionGroup);
    m_timeoutSpin->setObjectName(QStringLiteral("timeoutSpin"));
    m_timeoutSpin->setRange(0, 600000);
    m_timeoutSpin->setSpecialValueText(QStringLiteral("Predeterminado"));
    addFormRowWithHint(sessionForm, QStringLiteral("Tiempo de espera (ms):"), m_timeoutSpin,
                       QStringLiteral("/timeout:<ms>"));
    m_autoReconnectRetriesSpin = new QSpinBox(sessionGroup);
    m_autoReconnectRetriesSpin->setObjectName(QStringLiteral("autoReconnectRetriesSpin"));
    m_autoReconnectRetriesSpin->setRange(-1, 1000);
    m_autoReconnectRetriesSpin->setSpecialValueText(QStringLiteral("Predeterminado"));
    addFormRowWithHint(sessionForm, QStringLiteral("Reintentos de reconexión:"),
                       m_autoReconnectRetriesSpin,
                       QStringLiteral("/auto-reconnect-max-retries:<n>"));
    m_tlsSeclevelSpin = new QSpinBox(sessionGroup);
    m_tlsSeclevelSpin->setObjectName(QStringLiteral("tlsSeclevelSpin"));
    m_tlsSeclevelSpin->setRange(0, 5);
    m_tlsSeclevelSpin->setSpecialValueText(QStringLiteral("Predeterminado"));
    addFormRowWithHint(sessionForm, QStringLiteral("Nivel de seguridad TLS:"), m_tlsSeclevelSpin,
                       QStringLiteral("/tls:seclevel:<nivel>"));
    m_preventLockSpin = new QSpinBox(sessionGroup);
    m_preventLockSpin->setObjectName(QStringLiteral("preventLockSpin"));
    m_preventLockSpin->setRange(0, 3600);
    m_preventLockSpin->setSpecialValueText(QStringLiteral("Desactivado"));
    m_preventLockSpin->setSuffix(QStringLiteral(" s"));
    addFormRowWithHint(sessionForm, QStringLiteral("Evitar bloqueo de sesión:"), m_preventLockSpin,
                       QStringLiteral("/prevent-session-lock:<segundos>"));
    layout->addWidget(sessionGroup);

    auto* proxyGroup = new QGroupBox(QStringLiteral("Proxy"), page);
    auto* proxyForm = new QFormLayout(proxyGroup);
    m_proxyTypeCombo = new QComboBox(proxyGroup);
    m_proxyTypeCombo->setObjectName(QStringLiteral("proxyTypeCombo"));
    m_proxyTypeCombo->addItem(QStringLiteral("HTTP"), QStringLiteral("http"));
    m_proxyTypeCombo->addItem(QStringLiteral("SOCKS5"), QStringLiteral("socks5"));
    addFormRowWithHint(proxyForm, QStringLiteral("Tipo:"), m_proxyTypeCombo,
                       QStringLiteral("/proxy:<proto>://..."));
    m_proxyHostEdit = new QLineEdit(proxyGroup);
    m_proxyHostEdit->setObjectName(QStringLiteral("proxyHostEdit"));
    addFormRowWithHint(proxyForm, QStringLiteral("Servidor:"), m_proxyHostEdit,
                       QStringLiteral("/proxy:[proto://][usuario:contraseña@]<servidor>[:puerto]"));
    m_proxyPortSpin = new QSpinBox(proxyGroup);
    m_proxyPortSpin->setObjectName(QStringLiteral("proxyPortSpin"));
    m_proxyPortSpin->setRange(0, 65535);
    m_proxyPortSpin->setSpecialValueText(QStringLiteral("Predeterminado"));
    addFormRowWithHint(proxyForm, QStringLiteral("Puerto:"), m_proxyPortSpin,
                       QStringLiteral("/proxy:...:<puerto>"));
    m_proxyUserEdit = new QLineEdit(proxyGroup);
    m_proxyUserEdit->setObjectName(QStringLiteral("proxyUserEdit"));
    addFormRowWithHint(proxyForm, QStringLiteral("Usuario:"), m_proxyUserEdit,
                       QStringLiteral("/proxy:<usuario>@..."));
    m_proxyPasswordEdit = new QLineEdit(proxyGroup);
    m_proxyPasswordEdit->setObjectName(QStringLiteral("proxyPasswordEdit"));
    m_proxyPasswordEdit->setEchoMode(QLineEdit::Password);
    addFormRowWithHint(proxyForm, QStringLiteral("Contraseña:"), m_proxyPasswordEdit,
                       QStringLiteral("/proxy:...:<contraseña>@..."));
    layout->addWidget(proxyGroup);

    auto* developerGroup = new QGroupBox(QStringLiteral("Desarrollo"), page);
    auto* developerLayout = new QVBoxLayout(developerGroup);
    m_developerModeCheck = new WrappingCheckBox(
        QStringLiteral("Modo desarrollo (mostrar el comando de xfreerdp)"), developerGroup);
    m_developerModeCheck->setObjectName(QStringLiteral("developerModeCheck"));
    addWidgetWithHint(developerLayout, m_developerModeCheck,
                      QStringLiteral("(Solo interfaz: no envía parámetros a xfreerdp)"));
    m_commandPreviewEdit = new QPlainTextEdit(developerGroup);
    m_commandPreviewEdit->setObjectName(QStringLiteral("commandPreviewEdit"));
    m_commandPreviewEdit->setReadOnly(true);
    m_commandPreviewEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_commandPreviewEdit->setMinimumHeight(70);
    QFont monospace(QStringLiteral("monospace"));
    monospace.setStyleHint(QFont::Monospace);
    m_commandPreviewEdit->setFont(monospace);
    m_commandPreviewEdit->setVisible(false);
    developerLayout->addWidget(m_commandPreviewEdit);
    layout->addWidget(developerGroup);
    layout->addStretch(1);

    connect(m_gatewayCheck, &QCheckBox::toggled, m_gatewayHostEdit, &QLineEdit::setEnabled);
    connect(m_gatewayCheck, &QCheckBox::toggled, m_gatewayPortSpin, &QSpinBox::setEnabled);
    connect(m_gatewayCheck, &QCheckBox::toggled, m_gatewaySameCredentialsCheck,
            &QCheckBox::setEnabled);
    connect(m_gatewayCheck, &QCheckBox::toggled, m_gatewayUserEdit, &QLineEdit::setEnabled);
    connect(m_gatewaySameCredentialsCheck, &QCheckBox::toggled, m_gatewayUserEdit,
            [this](bool same) { m_gatewayUserEdit->setEnabled(m_gatewayCheck->isChecked() && !same); });
    connect(m_remoteAppCheck, &QCheckBox::toggled, m_remoteProgramEdit, &QLineEdit::setEnabled);
    connect(m_developerModeCheck, &QCheckBox::toggled, this,
            &ConnectionDialog::updateDeveloperHints);
    return page;
}

void ConnectionDialog::updateCommandPreview()
{
    if (!m_commandPreviewEdit || !m_developerModeCheck)
        return;

    if (!m_developerModeCheck->isChecked())
    {
        m_commandPreviewEdit->clear();
        m_commandPreviewEdit->setVisible(false);
        return;
    }

    // Recolecta el formulario en una copia local para no alterar el borrador
    // del perfil mientras el usuario escribe (collectForm modifica m_profile).
    const ConnectionProfile saved = m_profile;
    collectForm();
    const ConnectionProfile preview = m_profile;
    m_profile = saved;

    SessionBackend::Credentials credentials;
    credentials.username = m_userEdit->text().trimmed();
    credentials.domain = m_domainEdit->text().trimmed();
    credentials.password = m_passwordEdit->text();

    const QStringList args = XFreeRdpLauncher::arguments(preview, credentials, false);
    const QString command = QStringLiteral("%1 %2")
                                .arg(XFreeRdpLauncher::programPath(),
                                     args.join(QLatin1Char(' ')));
    m_commandPreviewEdit->setPlainText(command);
    m_commandPreviewEdit->setVisible(true);
}

QLabel* ConnectionDialog::createDeveloperHintLabel(const QString& hint, QWidget* parent)
{
    auto* label = new QLabel(hint, parent);
    label->setProperty("developerHint", true);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    if (mono.pointSizeF() > 2.0)
        mono.setPointSizeF(mono.pointSizeF() * 0.9);
    label->setFont(mono);
    label->setStyleSheet(QStringLiteral("color: #6b6b6b;"));
    label->setContentsMargins(22, 0, 0, 6);
    label->setVisible(false);
    return label;
}

void ConnectionDialog::registerDeveloperHint(QWidget* control, QLabel* label)
{
    if (!control || !label)
        return;

    m_developerHints.append(qMakePair(control, label));
    control->installEventFilter(this);
}

QLabel* ConnectionDialog::addWidgetWithHint(QBoxLayout* layout, QWidget* control, const QString& hint)
{
    layout->addWidget(control);
    auto* label = createDeveloperHintLabel(hint, layout->parentWidget());
    layout->addWidget(label);
    registerDeveloperHint(control, label);
    return label;
}

QLabel* ConnectionDialog::addFormRowWithHint(QFormLayout* form, const QString& labelText,
                                             QWidget* control, const QString& hint)
{
    form->addRow(labelText, control);
    auto* label = createDeveloperHintLabel(hint, form->parentWidget());
    form->addRow(label);
    registerDeveloperHint(control, label);
    return label;
}

void ConnectionDialog::addHintRow(QFormLayout* form, QWidget* control, const QString& hint)
{
    auto* label = createDeveloperHintLabel(hint, form->parentWidget());
    form->addRow(label);
    registerDeveloperHint(control, label);
}

void ConnectionDialog::updateDeveloperHints()
{
    const bool developer =
        m_developerModeCheck && m_developerModeCheck->isChecked();
    for (const auto& entry : m_developerHints)
    {
        const bool enabled = entry.first && entry.first->isEnabled();
        entry.second->setVisible(developer && enabled);
    }
}

bool ConnectionDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::EnabledChange)
    {
        if (auto* control = qobject_cast<QWidget*>(watched))
        {
            const bool developer =
                m_developerModeCheck && m_developerModeCheck->isChecked();
            for (const auto& entry : m_developerHints)
            {
                if (entry.first == control)
                    entry.second->setVisible(developer && control->isEnabled());
            }
        }
    }
    return QDialog::eventFilter(watched, event);
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
    m_clipboardFilesCheck->setChecked(profile.redirectClipboardFiles);
    m_clipboardFilesCheck->setEnabled(profile.redirectClipboard);
    m_drivesCheck->setChecked(profile.redirectDrives);
    m_printersCheck->setChecked(profile.redirectPrinters);
    m_homeDriveCheck->setChecked(profile.redirectHomeDrive);
    m_multitouchCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("multitouch"), false).toBool());
    m_drivesEdit->setText(profile.drives.join(QLatin1Char(';')));
    m_audioCheck->setChecked(profile.redirectAudio);
    m_microphoneCheck->setChecked(profile.redirectMicrophone);
    m_dynamicResolutionCheck->setChecked(profile.dynamicResolution);
    m_multimonCheck->setChecked(profile.useMultimon);
    m_spanMonitorsCheck->setChecked(profile.spanMonitors);
    m_smartSizingCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("smartSizing"), false).toBool());
    m_scaleCombo->setCurrentIndex(qMax(0, m_scaleCombo->findData(profile.scaleFactor)));
    m_orientationCombo->setCurrentIndex(qMax(
        0, m_orientationCombo->findData(
               profile.extraOptions.value(QStringLiteral("orientation"), 0))));
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
    m_windowDragCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("windowDrag"), false).toBool());
    m_compressionCombo->setCurrentIndex(qMax(
        0, m_compressionCombo->findData(
               profile.extraOptions.value(QStringLiteral("compressionLevel"), -1))));
    m_gdiCombo->setCurrentIndex(qMax(
        0, m_gdiCombo->findData(
               profile.extraOptions.value(QStringLiteral("gdi"),
                                          QStringLiteral("default")).toString())));
    m_rfxCheck->setChecked(profile.extraOptions.value(QStringLiteral("rfx"), false).toBool());
    m_nscCheck->setChecked(profile.extraOptions.value(QStringLiteral("nsc"), false).toBool());
    m_jpegCheck->setChecked(profile.extraOptions.value(QStringLiteral("jpeg"), false).toBool());
    m_videoCheck->setChecked(profile.extraOptions.value(QStringLiteral("video"), false).toBool());

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
    m_developerModeCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("developerMode"), false).toBool());

    m_secProtocolCombo->setCurrentIndex(qMax(
        0, m_secProtocolCombo->findData(
               profile.extraOptions.value(QStringLiteral("securityProtocol"),
                                          QStringLiteral("default")).toString())));
    m_adminCheck->setChecked(profile.extraOptions.value(QStringLiteral("admin"), false).toBool());
    m_restrictedAdminCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("restrictedAdmin"), false).toBool());
    m_workareaCheck->setChecked(
        profile.extraOptions.value(QStringLiteral("workarea"), false).toBool());
    m_shellEdit->setText(profile.shell);
    m_shellDirEdit->setText(
        profile.extraOptions.value(QStringLiteral("shellWorkingDirectory")).toString());
    m_clientHostnameEdit->setText(
        profile.extraOptions.value(QStringLiteral("clientHostname")).toString());
    m_timeoutSpin->setValue(profile.extraOptions.value(QStringLiteral("timeout"), 0).toInt());
    m_autoReconnectRetriesSpin->setValue(
        profile.extraOptions.value(QStringLiteral("autoReconnectMaxRetries"), -1).toInt());
    m_tlsSeclevelSpin->setValue(
        profile.extraOptions.value(QStringLiteral("tlsSeclevel"), 0).toInt());
    m_preventLockSpin->setValue(
        profile.extraOptions.value(QStringLiteral("preventSessionLock"), 0).toInt());
    m_proxyTypeCombo->setCurrentIndex(qMax(
        0, m_proxyTypeCombo->findData(profile.proxyType.isEmpty()
                                          ? QStringLiteral("http")
                                          : profile.proxyType)));
    m_proxyHostEdit->setText(profile.proxyHost);
    m_proxyPortSpin->setValue(profile.proxyPort);
    m_proxyUserEdit->setText(profile.proxyUsername);
    m_proxyPasswordEdit->setText(profile.proxyPassword);

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
    updateCommandPreview();
    updateDeveloperHints();
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
    m_profile.redirectClipboardFiles = m_clipboardFilesCheck->isChecked();
    m_profile.redirectDrives = m_drivesCheck->isChecked();
    m_profile.redirectPrinters = m_printersCheck->isChecked();
    m_profile.redirectHomeDrive = m_homeDriveCheck->isChecked();
    m_profile.drives = m_drivesEdit->text().split(QLatin1Char(';'), Qt::SkipEmptyParts);
    m_profile.redirectAudio = m_audioCheck->isChecked();
    m_profile.redirectMicrophone = m_microphoneCheck->isChecked();
    m_profile.dynamicResolution = m_dynamicResolutionCheck->isChecked();
    m_profile.useMultimon = m_multimonCheck->isChecked();
    m_profile.spanMonitors = m_spanMonitorsCheck->isChecked();
    m_profile.scaleFactor = m_scaleCombo->currentData().toInt();
    m_profile.colorDepth = m_colorDepthCombo->currentData().toInt();
    m_profile.networkType = m_networkCombo->currentData().toString();
    m_profile.wallpaper = m_wallpaperCheck->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
    m_profile.fontSmoothing = m_fontSmoothingCheck->isChecked();
    m_profile.desktopComposition = m_desktopCompositionCheck->isChecked();
    m_profile.animations = m_animationsCheck->isChecked();
    m_profile.shell = m_shellEdit->text().trimmed();
    m_profile.proxyType = m_proxyTypeCombo->currentData().toString();
    m_profile.proxyHost = m_proxyHostEdit->text().trimmed();
    m_profile.proxyPort = m_proxyPortSpin->value();
    m_profile.proxyUsername = m_proxyUserEdit->text().trimmed();
    m_profile.proxyPassword = m_proxyPasswordEdit->text();
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
    m_profile.extraOptions.insert(QStringLiteral("developerMode"),
                                  m_developerModeCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("smartSizing"), m_smartSizingCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("orientation"),
                                  m_orientationCombo->currentData());
    m_profile.extraOptions.insert(QStringLiteral("multitouch"), m_multitouchCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("windowDrag"), m_windowDragCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("compressionLevel"),
                                  m_compressionCombo->currentData());
    m_profile.extraOptions.insert(QStringLiteral("gdi"), m_gdiCombo->currentData().toString());
    m_profile.extraOptions.insert(QStringLiteral("rfx"), m_rfxCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("nsc"), m_nscCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("jpeg"), m_jpegCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("video"), m_videoCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("securityProtocol"),
                                  m_secProtocolCombo->currentData().toString());
    m_profile.extraOptions.insert(QStringLiteral("admin"), m_adminCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("restrictedAdmin"),
                                  m_restrictedAdminCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("workarea"), m_workareaCheck->isChecked());
    m_profile.extraOptions.insert(QStringLiteral("shellWorkingDirectory"),
                                  m_shellDirEdit->text().trimmed());
    m_profile.extraOptions.insert(QStringLiteral("clientHostname"),
                                  m_clientHostnameEdit->text().trimmed());
    m_profile.extraOptions.insert(QStringLiteral("timeout"), m_timeoutSpin->value());
    m_profile.extraOptions.insert(QStringLiteral("autoReconnectMaxRetries"),
                                  m_autoReconnectRetriesSpin->value());
    m_profile.extraOptions.insert(QStringLiteral("tlsSeclevel"), m_tlsSeclevelSpin->value());
    m_profile.extraOptions.insert(QStringLiteral("preventSessionLock"),
                                  m_preventLockSpin->value());
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
    auto* remember = new WrappingCheckBox(QStringLiteral("Recordar contraseña"), &dialog);
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
