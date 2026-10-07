#include "core/CredentialStore.h"
#include "core/ProfileStore.h"
#include "core/SessionBackend.h"
#include "ui/ConnectionDialog.h"
#include "ui/SessionView.h"
#include "ui/SessionWindow.h"

#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QGuiApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QListWidget>
#include <QTabWidget>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace qtrdp;

namespace {

class StubBackend final : public SessionBackend
{
public:
    using SessionBackend::SessionBackend;

    QString protocol() const override
    {
        return QStringLiteral("stub");
    }

    QString displayName() const override
    {
        return QStringLiteral("Stub");
    }

    bool start(const ConnectionProfile&, const Credentials&) override
    {
        running = true;
        return true;
    }

    void requestStop() override
    {
        running = false;
    }

    void sendKeyEvent(const KeyEvent& event) override
    {
        keys.append(event);
    }

    void sendPointerEvent(const PointerEvent& event) override
    {
        pointers.append(event);
    }

    void sendText(const QString& text) override
    {
        lastText = text;
    }

    void sendClipboardText(const QString& text) override
    {
        lastClipboard = text;
    }

    void sendCtrlAltDel() override
    {
        ctrlAltDelCount++;
    }

    void requestDesktopSize(const QSize& size) override
    {
        requestedSize = size;
    }

    QSize desktopSize() const override
    {
        return QSize(1024, 768);
    }

    SessionState state() const override
    {
        return running ? SessionState::Connected : SessionState::Disconnected;
    }

    QString userName() const override
    {
        return QStringLiteral("prueba");
    }

    QString domain() const override
    {
        return QStringLiteral("LAB");
    }

    QString hostName() const override
    {
        return QStringLiteral("servidor");
    }

    QList<SessionCapability> capabilities() const override
    {
        return { SessionCapability::Clipboard, SessionCapability::DynamicResolution };
    }

    bool handlesKeyCombination(const KeyCombination& combination) const override
    {
        return combination.key == Qt::Key_Delete &&
               combination.modifiers.testFlag(Qt::ControlModifier) &&
               combination.modifiers.testFlag(Qt::AltModifier);
    }

    void handleKeyCombination(const KeyCombination&) override
    {
        handledCombinations++;
    }

    bool running = false;
    int ctrlAltDelCount = 0;
    int handledCombinations = 0;
    QString lastText;
    QString lastClipboard;
    QSize requestedSize;
    QList<KeyEvent> keys;
    QList<PointerEvent> pointers;
};

class FailingCredentialStore final : public CredentialStore
{
public:
    bool isAvailable() const override { return true; }
    QString backendName() const override { return QStringLiteral("almacén de prueba"); }
    QString read(const QString&, QString*) const override { return {}; }
    bool write(const QString&, const QString&, QString* error) override
    {
        ++writes;
        if (error)
            *error = QStringLiteral("backend rechazó la escritura");
        return false;
    }
    bool remove(const QString&, QString*) override { return true; }
    int writes = 0;
};

} // namespace

class TestUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qApp->setStyle(QStringLiteral("Fusion"));
    }

    void connectionDialogListsAndSelectsProfiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());

        ConnectionProfile first;
        first.name = QStringLiteral("Escritorio");
        first.host = QStringLiteral("10.0.0.5");
        first.username = QStringLiteral("soporte");
        first.desktopWidth = 1280;
        first.desktopHeight = 1024;

        ConnectionProfile second;
        second.name = QStringLiteral("Servidor");
        second.host = QStringLiteral("10.0.0.6");
        second.port = 3390;
        second.username = QStringLiteral("admin");
        second.domain = QStringLiteral("LAB");

        {
            ProfileStore store;
            QVERIFY(store.add(first));
            QVERIFY(store.add(second));
        }

        ConnectionDialog dialog;

        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("profileList"));
        auto* host = dialog.findChild<QLineEdit*>(QStringLiteral("hostEdit"));
        auto* user = dialog.findChild<QLineEdit*>(QStringLiteral("userEdit"));
        auto* port = dialog.findChild<QSpinBox*>(QStringLiteral("portSpin"));
        auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("profileNameEdit"));
        QVERIFY(list && host && user && port && name);
        QCOMPARE(list->count(), 2);

        // Al abrirse se selecciona el primer perfil y el formulario se rellena.
        QCOMPARE(list->currentItem()->text(), QStringLiteral("Escritorio"));
        QCOMPARE(host->text(), QStringLiteral("10.0.0.5"));
        QCOMPARE(user->text(), QStringLiteral("soporte"));
        QCOMPARE(port->value(), 3389);
        QCOMPARE(name->text(), QStringLiteral("Escritorio"));

        // Al elegir otro perfil cambia todo el formulario.
        list->setCurrentRow(1);
        QCOMPARE(list->currentItem()->text(), QStringLiteral("Servidor"));
        QCOMPARE(host->text(), QStringLiteral("10.0.0.6"));
        QCOMPARE(user->text(), QStringLiteral("admin"));
        QCOMPARE(port->value(), 3390);

        // Renombrar en el formulario actualiza la lista de inmediato.
        name->setText(QStringLiteral("Servidor de pruebas"));
        QCOMPARE(list->currentItem()->text(), QStringLiteral("Servidor de pruebas"));

        // ...y se guarda en cuanto el usuario termina de escribir.
        name->editingFinished();
        {
            ProfileStore store;
            QCOMPARE(store.profile(list->currentItem()->data(Qt::UserRole).toString()).name,
                     QStringLiteral("Servidor de pruebas"));
        }

        // Los botones de acción dependen de la selección.
        auto* up = dialog.findChild<QToolButton*>(QStringLiteral("moveUpButton"));
        auto* down = dialog.findChild<QToolButton*>(QStringLiteral("moveDownButton"));
        auto* remove = dialog.findChild<QToolButton*>(QStringLiteral("deleteProfileButton"));
        auto* create = dialog.findChild<QToolButton*>(QStringLiteral("newProfileButton"));
        QVERIFY(up && down && remove && create);
        QVERIFY(up->isEnabled());
        QVERIFY(!down->isEnabled());

        // Subir el perfil lo reordena y lo guarda.
        up->click();
        QCOMPARE(list->currentRow(), 0);
        QVERIFY(!up->isEnabled());
        {
            ProfileStore store;
            QCOMPARE(store.profiles().at(0).name, QStringLiteral("Servidor de pruebas"));
        }

        // Crear un perfil nuevo lo añade al final.
        create->click();
        QCOMPARE(list->count(), 3);
        QCOMPARE(list->currentRow(), 2);
        {
            ProfileStore store;
            QCOMPARE(store.count(), 3);
        }

        // QTRDP_TEST_SNAPSHOT_DIR guarda una captura del diálogo.
        const QString snapshotDir =
	    QString::fromLocal8Bit(qgetenv("QTRDP_TEST_SNAPSHOT_DIR"));
        if (!snapshotDir.isEmpty())
        {
            QVERIFY(QDir(snapshotDir).mkpath(QStringLiteral(".")));
            QVERIFY(dialog.grab().save(QDir(snapshotDir).filePath(
                QStringLiteral("dialogo-conexion.png"))));
            auto* toggle = dialog.findChild<QToolButton*>(QStringLiteral("optionsToggleButton"));
            QVERIFY(toggle);
            toggle->click();
            QCoreApplication::processEvents();
            QVERIFY(dialog.grab().save(QDir(snapshotDir).filePath(
                QStringLiteral("dialogo-opciones.png"))));
        }
    }

    void connectionDialogValidatesHostAndCreatesProfiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());

        ConnectionDialog dialog;
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("profileList"));
        auto* host = dialog.findChild<QLineEdit*>(QStringLiteral("hostEdit"));
        auto* error = dialog.findChild<QLabel*>(QStringLiteral("errorLabel"));
        auto* password =
	    dialog.findChild<QLineEdit*>(QStringLiteral("passwordEdit"));
        auto* remember =
	    dialog.findChild<QCheckBox*>(QStringLiteral("rememberPasswordCheck"));
        auto* buttons =
	    dialog.findChild<QDialogButtonBox*>();
        QVERIFY(list && host && error && password && remember && buttons);
        QCOMPARE(list->count(), 0);

        // Sin equipo el botón Conectar se bloquea y no se guarda nada.
        host->setText(QStringLiteral("   "));
        QVERIFY(!buttons->button(QDialogButtonBox::Ok)->isEnabled());
        QVERIFY(!error->isVisibleTo(&dialog));
        QCOMPARE(dialog.result(), 0);
        QCOMPARE(ProfileStore().count(), 0);

        // Con equipo, conectar crea el perfil y lo persiste.
        host->setText(QStringLiteral("equipo.lab.local"));
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
        remember->setChecked(true);
        password->setText(QStringLiteral("secreto-de-prueba"));
        QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(ProfileStore().count(), 1);
        QCOMPARE(dialog.profile().host, QStringLiteral("equipo.lab.local"));
        QCOMPARE(dialog.credentials().password, QStringLiteral("secreto-de-prueba"));

        // La contraseña quedó guardada fuera del perfil.
        ProfileStore store;
        const ConnectionProfile saved = store.profiles().first();
        QVERIFY(saved.rememberPassword);
        QCOMPARE(CredentialStore::instance()->read(CredentialStore::credentialId(saved)),
                 QStringLiteral("secreto-de-prueba"));
        QVERIFY(!saved.extraOptions.contains(QStringLiteral("password")));
        QStringList extras;
        for (const QVariant& value : saved.extraOptions)
            extras.append(value.toString());
        QVERIFY(!extras.join(QLatin1Char('|')).contains(QStringLiteral("secreto-de-prueba")));

        // La última credencial de la prueba se limpia.
        CredentialStore::instance()->remove(CredentialStore::credentialId(saved));
    }

    void connectionDialogMatchesRemoteDesktopOptionsLayout()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());

        ConnectionProfile profile;
        profile.name = QStringLiteral("Escritorio");
        profile.host = QStringLiteral("10.0.0.5");
        profile.username = QStringLiteral("soporte");
        {
            ProfileStore store;
            QVERIFY(store.add(profile));
        }

        ConnectionDialog dialog;
        dialog.resize(430, 285);
        dialog.show();
        QCoreApplication::processEvents();

        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("profileList"));
        auto* host = dialog.findChild<QLineEdit*>(QStringLiteral("hostEdit"));
        auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("connectionOptionsTabs"));
        auto* quickHost = dialog.findChild<QComboBox*>(QStringLiteral("quickHostCombo"));
        auto* toggle = dialog.findChild<QToolButton*>(QStringLiteral("optionsToggleButton"));
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(list && host && tabs && quickHost && toggle && buttons);

        QVERIFY(quickHost->isVisible());
        QVERIFY(!tabs->isVisible());
        QCOMPARE(toggle->text(), QStringLiteral("Mostrar opciones"));
        QCOMPARE(buttons->button(QDialogButtonBox::Ok)->text(), QStringLiteral("Conectar"));

        toggle->click();
        QCoreApplication::processEvents();
        QVERIFY(!quickHost->isVisible());
        QVERIFY(tabs->isVisible());
        QCOMPARE(tabs->count(), 5);
        QCOMPARE(tabs->tabText(0), QStringLiteral("General"));
        QCOMPARE(tabs->tabText(1), QStringLiteral("Pantalla"));
        QCOMPARE(tabs->tabText(2), QStringLiteral("Recursos locales"));
        QCOMPARE(tabs->tabText(3), QStringLiteral("Rendimiento"));
        QCOMPARE(tabs->tabText(4), QStringLiteral("Opciones avanzadas"));
        QCOMPARE(toggle->text(), QStringLiteral("Ocultar opciones"));
        QVERIFY(host->isVisible());
        QVERIFY(list->isVisible());

        // Los controles se agrupan en la página General y la barra de acciones
        // permanece debajo de las pestañas.
        const QPoint tabsPos = tabs->mapTo(&dialog, QPoint(0, 0));
        const QPoint buttonPos = buttons->mapTo(&dialog, QPoint(0, 0));
        QVERIFY(tabsPos.y() > 0);
        QVERIFY(buttonPos.y() > tabsPos.y() + tabs->height() - 1);
        QVERIFY(list->width() >= 150);
        QVERIFY(list->height() >= 60);
        QVERIFY(host->width() >= 200);
        QCOMPARE(buttons->button(QDialogButtonBox::Ok)->text(), QStringLiteral("Conectar"));

        dialog.hide();
    }

    void connectionDialogQuickAndExpandedViewsStaySynchronized()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());

        ConnectionDialog dialog;
        auto* quickHost = dialog.findChild<QComboBox*>(QStringLiteral("quickHostCombo"));
        auto* host = dialog.findChild<QLineEdit*>(QStringLiteral("hostEdit"));
        auto* user = dialog.findChild<QLineEdit*>(QStringLiteral("userEdit"));
        auto* domain = dialog.findChild<QLineEdit*>(QStringLiteral("domainEdit"));
        auto* userLabel = dialog.findChild<QLabel*>(QStringLiteral("quickUserLabel"));
        auto* toggle = dialog.findChild<QToolButton*>(QStringLiteral("optionsToggleButton"));
        QVERIFY(quickHost && host && user && domain && userLabel && toggle);

        quickHost->setEditText(QStringLiteral("rdp.example.test"));
        QCOMPARE(host->text(), QStringLiteral("rdp.example.test"));
        toggle->click();
        user->setText(QStringLiteral("soporte"));
        domain->setText(QStringLiteral("LAB"));
        QCOMPARE(userLabel->text(), QStringLiteral("Usuario:    LAB\\soporte"));
        toggle->click();
        QCOMPARE(quickHost->currentText(), QStringLiteral("rdp.example.test"));
    }

    void connectionDialogRemembersPasswordWithReminder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        CredentialStore::setInstance(CredentialStore::createFileStore());

        ConnectionProfile saved;
        saved.name = QStringLiteral("Escritorio");
        saved.host = QStringLiteral("10.0.0.5");
        saved.username = QStringLiteral("soporte");
        saved.rememberPassword = true;
        {
            ProfileStore store;
            QVERIFY(store.add(saved));
            QString error;
            QVERIFY2(CredentialStore::instance()->write(
                         CredentialStore::credentialId(saved), QStringLiteral("clave-guardada"),
                         &error),
                     qPrintable(error));
        }

        ConnectionDialog dialog;
        auto* list = dialog.findChild<QListWidget*>(QStringLiteral("profileList"));
        auto* password = dialog.findChild<QLineEdit*>(QStringLiteral("passwordEdit"));
        auto* remember =
	    dialog.findChild<QCheckBox*>(QStringLiteral("rememberPasswordCheck"));
        auto* hint = dialog.findChild<QLabel*>(QStringLiteral("passwordHint"));
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(list && password && remember && buttons);
        QVERIFY(hint);

        // El perfil seleccionado tiene marcada la casilla y la contraseña
        // sigue oculta aunque exista en el almacén.
        QCOMPARE(list->count(), 1);
        QVERIFY(remember->isChecked());
        QCOMPARE(password->text(), QString());
        QCOMPARE(password->echoMode(), QLineEdit::Password);
        QVERIFY(hint->text().contains(QStringLiteral("guardada")));

        // Al conectar se recupera del almacén sin mostrarla en pantalla.
        QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.credentials().password, QStringLiteral("clave-guardada"));
        QCOMPARE(dialog.credentials().username, QStringLiteral("soporte"));

        // Si el usuario escribe la contraseña sin marcar la casilla, se le avisa.
        remember->setChecked(false);
        password->setText(QStringLiteral("otra"));
        QVERIFY(hint->text().contains(QStringLiteral("Recordar")));

        CredentialStore::instance()->remove(CredentialStore::credentialId(saved));
        CredentialStore::setInstance(nullptr);
    }

    void connectionDialogAllowsPasswordPromptAfterProfileValidation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        CredentialStore::setInstance(CredentialStore::createFileStore());

        ConnectionProfile profile;
        profile.name = QStringLiteral("Servidor");
        profile.host = QStringLiteral("10.0.0.6");
        profile.username = QStringLiteral("soporte");
        profile.rememberPassword = true; // Marcada pero sin contraseña guardada.
        {
            ProfileStore store;
            QVERIFY(store.add(profile));
        }

        ConnectionDialog dialog;
        auto* error = dialog.findChild<QLabel*>(QStringLiteral("errorLabel"));
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(error && buttons);
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());

        // El gestor de perfiles no bloquea la conexión: main.cpp solicitará la
        // contraseña justo antes de iniciar la sesión.
        QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!error->isVisibleTo(&dialog));

        CredentialStore::setInstance(nullptr);
    }

    void connectionDialogReportsPasswordSaveFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        auto* failingStore = new FailingCredentialStore;
        CredentialStore::setInstance(failingStore);

        ConnectionDialog dialog;
        auto* host = dialog.findChild<QLineEdit*>(QStringLiteral("hostEdit"));
        auto* password = dialog.findChild<QLineEdit*>(QStringLiteral("passwordEdit"));
        auto* remember = dialog.findChild<QCheckBox*>(QStringLiteral("rememberPasswordCheck"));
        auto* error = dialog.findChild<QLabel*>(QStringLiteral("errorLabel"));
        auto* buttons = dialog.findChild<QDialogButtonBox*>();
        QVERIFY(host && password && remember && error && buttons);

        host->setText(QStringLiteral("servidor.example.test"));
        password->setText(QStringLiteral("secreto"));
        remember->setChecked(true);
        QVERIFY(remember->isChecked());
        QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);

        QCOMPARE(failingStore->writes, 1);
        QCOMPARE(dialog.result(), 0);
        QVERIFY(error->isVisibleTo(&dialog));
        QVERIFY(error->text().contains(QStringLiteral("backend rechazó la escritura")));
        CredentialStore::setInstance(nullptr);
    }

    void windowDisplaysFrames()
    {
        StubBackend backend;
        ConnectionProfile profile;
        profile.host = QStringLiteral("servidor");

        SessionWindow window(&backend, profile);
        window.resize(900, 700);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        SessionView* view = window.findChild<SessionView*>();
        QVERIFY(view);
        QCOMPARE(view->desktopSize(), QSize(1024, 768));

        QImage frame(1024, 768, QImage::Format_RGB32);
        frame.fill(QColor(0, 128, 255));
        Q_EMIT backend.frameReady(frame);

        QCOMPARE(view->desktopSize(), QSize(1024, 768));

        const QPixmap painted = view->grab();
        QCOMPARE(painted.size(), view->size());
        QCOMPARE(painted.toImage().pixelColor(10, 10), QColor(0, 128, 255));
    }

    void resizingRequestsDesktopSizeAfterDebounce()
    {
        StubBackend backend;
        SessionView view(&backend);
        QCOMPARE(view.desktopSize(), QSize(1024, 768));
        QVERIFY(view.dynamicResolution());

        view.resize(1400, 1000);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        // Durante el antirrebote todavía no se ha enviado nada.
        QTest::qWait(50);
        QCOMPARE(backend.requestedSize, QSize());

        // Pasado el antirrebote se pide el tamaño exacto de la ventana.
        QTRY_VERIFY_WITH_TIMEOUT(backend.requestedSize == view.size(), 2000);
        QCOMPARE(backend.requestedSize, QSize(1400, 1000));

        // Un ajuste mínimo no genera otra petición.
        backend.requestedSize = QSize();
        view.resize(1404, 1004);
        QTest::qWait(600);
        QCOMPARE(backend.requestedSize, QSize());

        // Un cambio real vuelve a pedir el tamaño.
        view.resize(1520, 1040);
        QTRY_VERIFY_WITH_TIMEOUT(backend.requestedSize == QSize(1520, 1040), 2000);

        // Repetir el mismo tamaño tampoco genera peticiones repetidas.
        backend.requestedSize = QSize();
        view.resize(1520, 1040);
        QTest::qWait(600);
        QCOMPARE(backend.requestedSize, QSize());
    }

    void dynamicResolutionCanBeDisabled()
    {
        StubBackend backend;
        ConnectionProfile profile;
        profile.host = QStringLiteral("servidor");

        SessionWindow window(&backend, profile);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        SessionView* view = window.findChild<SessionView*>();
        QVERIFY(view);

        view->setDynamicResolution(false);
        window.resize(1400, 1000);
        QTest::qWait(600);
        QCOMPARE(backend.requestedSize, QSize());
    }

    void keyboardIsForwarded()
    {
        StubBackend backend;
        ConnectionProfile profile;
        profile.host = QStringLiteral("servidor");

        SessionWindow window(&backend, profile);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        SessionView* view = window.findChild<SessionView*>();
        QVERIFY(view);
        view->setFocus();

        QTest::keyClick(view, Qt::Key_A);
        QCOMPARE(backend.keys.count(), 2);
        QCOMPARE(backend.keys.at(0).key, static_cast<int>(Qt::Key_A));
        QCOMPARE(backend.keys.at(0).action, KeyAction::Press);
        QCOMPARE(backend.keys.at(1).action, KeyAction::Release);
        QCOMPARE(backend.handledCombinations, 0);
    }

    void reservedCombinationIsNotForwarded()
    {
        StubBackend backend;
        ConnectionProfile profile;
        profile.host = QStringLiteral("servidor");

        SessionWindow window(&backend, profile);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        SessionView* view = window.findChild<SessionView*>();
        QVERIFY(view);
        view->setFocus();

        QTest::keyClick(view, Qt::Key_Delete, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(backend.handledCombinations, 1);
        QCOMPARE(backend.ctrlAltDelCount, 0);

        // Los modificadores sí se reenvían, la tecla de la combinación no.
        for (const KeyEvent& key : backend.keys)
            QVERIFY2(key.key != static_cast<int>(Qt::Key_Delete),
                     "Ctrl+Alt+Del no debe reenviarse como tecla normal");
    }

    void pointerCoordinatesAreScaledToDesktop()
    {
        StubBackend backend;
        ConnectionProfile profile;
        profile.host = QStringLiteral("servidor");

        SessionWindow window(&backend, profile);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        SessionView* view = window.findChild<SessionView*>();
        QVERIFY(view);

        QImage frame(1024, 768, QImage::Format_RGB32);
        frame.fill(Qt::black);
        Q_EMIT backend.frameReady(frame);

        const QSize viewSize = view->size();
        view->resize(viewSize);
        QTest::qWait(50);

        const QPoint local(viewSize.width() / 4, viewSize.height() / 4);
        const QPoint desktop = view->toDesktop(local);
        QVERIFY(desktop.x() > local.x());
        QVERIFY(desktop.y() > local.y());

        QTest::mousePress(view, Qt::LeftButton, {}, local);
        QVERIFY(!backend.pointers.isEmpty());
        QCOMPARE(backend.pointers.last().action, PointerAction::ButtonDown);
        QCOMPARE(backend.pointers.last().x, desktop.x());
        QCOMPARE(backend.pointers.last().y, desktop.y());
    }

    void wheelAndToolbarButtonsWork()
    {
        StubBackend backend;
        ConnectionProfile profile;
        profile.host = QStringLiteral("servidor");

        SessionWindow window(&backend, profile);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        SessionView* view = window.findChild<SessionView*>();
        QVERIFY(view);

        QWheelEvent wheel(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view, &wheel);
        QVERIFY(!backend.pointers.isEmpty());
        QCOMPARE(backend.pointers.last().action, PointerAction::Wheel);

        QToolButton* sasButton = nullptr;
        for (QToolButton* button : window.findChildren<QToolButton*>())
        {
            if (button->text() == QStringLiteral("Ctrl+Alt+Del"))
                sasButton = button;
        }
        QVERIFY(sasButton);
        sasButton->click();
        QCOMPARE(backend.ctrlAltDelCount, 1);

        QClipboard* clipboard = QGuiApplication::clipboard();
        clipboard->setText(QStringLiteral("texto local"));

        QToolButton* clipboardButton = nullptr;
        for (QToolButton* button : window.findChildren<QToolButton*>())
        {
            if (button->text() == QStringLiteral("Portapapeles"))
                clipboardButton = button;
        }
        QVERIFY(clipboardButton);
        // El backend stub anuncia Clipboard, así que el botón queda activo al conectar.
        Q_EMIT backend.stateChanged(SessionState::Connected);
        QVERIFY(clipboardButton->isEnabled());
        clipboardButton->click();
        QCOMPARE(backend.lastClipboard, QStringLiteral("texto local"));
    }

    void windowClosesAndStopsBackend()
    {
        StubBackend backend;
        ConnectionProfile profile;
        profile.host = QStringLiteral("servidor");

        SessionWindow window(&backend, profile);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        QSignalSpy finishedSpy(&window, &SessionWindow::finished);
        window.close();
        QCOMPARE(finishedSpy.count(), 1);
        QVERIFY(!backend.running);
    }
};

QTEST_MAIN(TestUi)

#include "TestUi.moc"
