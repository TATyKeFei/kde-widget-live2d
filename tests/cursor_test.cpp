#include "cursorstate.h"
#include "desktoptracker.h"
#include "live2ditem.h"
#include <QFile>
#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtTest>
#include <limits>

class CursorTest : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void policies()
    {
        using namespace CursorTracking;
        for (const bool normal : {false, true}) {
            for (const bool fullscreen : {false, true}) {
                const State state{{}, normal, fullscreen, true};
                QVERIFY(!follows(Mode::Off, state));
                QCOMPARE(follows(Mode::Focused, state), normal);
                QCOMPARE(follows(Mode::NotFullscreen, state), !fullscreen);
                QVERIFY(follows(Mode::Always, state));
            }
        }
        for (const auto mode : {Mode::Off, Mode::Focused, Mode::NotFullscreen, Mode::Always})
            QVERIFY(!follows(mode, State{}));
    }

    void screenCoordinates()
    {
        using CursorTracking::gaze;
        const QSizeF screen(1920, 1080);
        const QPointF center(960, 540);
        QCOMPARE(gaze(center, center, screen), QPointF());
        QCOMPARE(gaze({1920, 0}, center, screen), QPointF(1, 1));
        QCOMPARE(gaze({0, 1080}, center, screen), QPointF(-1, -1));
        // Moving beyond the widget still changes gaze; other monitors saturate it.
        QCOMPARE(gaze({1440, 270}, center, screen), QPointF(.5, .5));
        QCOMPARE(gaze({6000, -2000}, center, screen), QPointF(1, 1));
        QCOMPARE(gaze({-480, 270}, {-960, 540}, screen), QPointF(.5, .5));
        QCOMPARE(gaze({1, 1}, center, {}), QPointF());
        QCOMPARE(gaze({std::numeric_limits<double>::quiet_NaN(), 1}, center, screen), QPointF());
        using CursorTracking::screenPosition;
        const QRectF leftScreen(-1920, 0, 1920, 1080);
        QCOMPARE(screenPosition({-960, 540}, leftScreen), QPointF());
        QCOMPARE(screenPosition({0, 0}, leftScreen), QPointF(1, 1));
        QCOMPARE(screenPosition({-1920, 1080}, leftScreen), QPointF(-1, -1));
        QCOMPARE(screenPosition({-1440, 270}, leftScreen), QPointF(-.5, .5));
        QCOMPARE(screenPosition({960, 540}, leftScreen), QPointF(1, 0));
    }

    void itemModes()
    {
        Live2DItem item;
        QCOMPARE(item.cursorFollowMode(), QStringLiteral("always"));
        QCOMPARE(item.mouseTrackingMode(), QStringLiteral("screen"));
        item.setMouseTrackingMode(QStringLiteral("relative"));
        item.setMouseTrackingMode(QStringLiteral("invalid"));
        QCOMPARE(item.mouseTrackingMode(), QStringLiteral("relative"));
        item.setRelativeSensitivity(2);
        QCOMPARE(item.relativeSensitivity(), 2.0);
        item.setRelativeSensitivity(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(item.relativeSensitivity(), 2.0);
        item.setRelativeSensitivity(100);
        QCOMPARE(item.relativeSensitivity(), 5.0);
        item.setCursorFollowMode(QStringLiteral("off"));
        QVERIFY(!item.followCursor());
        item.setCursorFollowMode(QStringLiteral("focused"));
        QVERIFY(item.followCursor());
        item.setCursorFollowMode(QStringLiteral("unknown"));
        QCOMPARE(item.cursorFollowMode(), QStringLiteral("focused"));
        item.setFollowCursor(false); // Backward-compatible checkbox API.
        QCOMPARE(item.cursorFollowMode(), QStringLiteral("off"));
        item.setFollowCursor(true);
        QCOMPARE(item.cursorFollowMode(), QStringLiteral("always"));
    }

    void configurationMigration()
    {
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(
            QStringLiteral("i18n"),
            QVariant::fromValue(engine.evaluate(QStringLiteral("(function (text) { return text; })"))));
        QQmlComponent component(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(LIVE2D_CONFIG_QML)).resolved(QUrl("configFeatures.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 3000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> config(component.create());
        QVERIFY2(config, qPrintable(component.errorString()));
        auto *selector = config->findChild<QObject *>(QStringLiteral("cursorFollowModeSelector"));
        QVERIFY(selector);
        QCOMPARE(selector->property("currentIndex").toInt(), 3);
        // Old installations with a disabled checkbox stay disabled on upgrade.
        config->setProperty("cfg_followCursor", false);
        QCOMPARE(selector->property("currentIndex").toInt(), 0);
        config->setProperty("cfg_cursorFollowMode", QStringLiteral("notFullscreen"));
        QCOMPARE(selector->property("currentIndex").toInt(), 2);
        selector->setProperty("currentIndex", 1);
        QVERIFY(QMetaObject::invokeMethod(selector, "activated", Q_ARG(int, 1)));
        QCOMPARE(config->property("cfg_cursorFollowMode").toString(), QStringLiteral("focused"));
        QVERIFY(config->property("cfg_followCursor").toBool());
        selector->setProperty("currentIndex", 0);
        QVERIFY(QMetaObject::invokeMethod(selector, "activated", Q_ARG(int, 0)));
        QCOMPARE(config->property("cfg_cursorFollowMode").toString(), QStringLiteral("off"));
        QVERIFY(!config->property("cfg_followCursor").toBool());
        auto *mouseMode = config->findChild<QObject *>(QStringLiteral("mouseTrackingModeSelector"));
        QVERIFY(mouseMode);
        QCOMPARE(mouseMode->property("currentIndex").toInt(), 0);
        mouseMode->setProperty("currentIndex", 1);
        QVERIFY(QMetaObject::invokeMethod(mouseMode, "activated", Q_ARG(int, 1)));
        QCOMPARE(config->property("cfg_mouseTrackingMode").toString(), QStringLiteral("relative"));
        auto *sensitivity = config->findChild<QObject *>(QStringLiteral("relativeSensitivitySelector"));
        QVERIFY(sensitivity);
        QCOMPARE(config->property("cfg_relativeSensitivity").toDouble(), 1.0);
        config->setProperty("cfg_relativeSensitivity", 2.5);
        QCOMPARE(sensitivity->property("value").toDouble(), 2.5);
        auto *input = config->findChild<QObject *>(QStringLiteral("inputModeSelector"));
        QVERIFY(input);
        QCOMPARE(config->property("cfg_inputMode").toString(), QStringLiteral("focused"));
        input->setProperty("currentIndex", 2);
        QVERIFY(QMetaObject::invokeMethod(input, "activated", Q_ARG(int, 2)));
        QCOMPARE(config->property("cfg_inputMode").toString(), QStringLiteral("global"));
        auto *keepAbove = config->findChild<QObject *>(QStringLiteral("keepAboveSelector"));
        QVERIFY(keepAbove);
        QVERIFY(!config->property("cfg_keepAbove").toBool());
        config->setProperty("cfg_keepAbove", true);
        QVERIFY(keepAbove->property("checked").toBool());
        keepAbove->setProperty("checked", false);
        QVERIFY(!config->property("cfg_keepAbove").toBool());
        auto *clickThrough = config->findChild<QObject *>(QStringLiteral("clickThroughSelector"));
        QVERIFY(clickThrough);
        QVERIFY(clickThrough->property("enabled").toBool());
        config->setProperty("cfg_clickThrough", true);
        QVERIFY(clickThrough->property("checked").toBool());
        QQmlComponent rendering(
            &engine,
            QUrl::fromLocalFile(QStringLiteral(LIVE2D_CONFIG_QML)).resolved(QUrl("configRendering.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(rendering.status() != QQmlComponent::Loading, 3000);
        QVERIFY2(rendering.isReady(), qPrintable(rendering.errorString()));
        std::unique_ptr<QObject> renderConfig(rendering.create());
        QVERIFY2(renderConfig, qPrintable(rendering.errorString()));
        auto *frameRate = renderConfig->findChild<QObject *>(QStringLiteral("frameRateSelector"));
        QVERIFY(frameRate);
        renderConfig->setProperty("cfg_frameRate", 0);
        QCOMPARE(frameRate->property("value").toInt(), 0);
        QTRY_COMPARE(frameRate->property("displayText").toString(), QStringLiteral("无限制"));
        renderConfig->setProperty("cfg_frameRate", 144);
        QCOMPARE(frameRate->property("value").toInt(), 144);
        frameRate->setProperty("value", 240);
        QCOMPARE(renderConfig->property("cfg_frameRate").toInt(), 240);
        QQmlComponent character(&engine, QUrl::fromLocalFile(QStringLiteral(LIVE2D_CONFIG_QML)));
        QTRY_VERIFY_WITH_TIMEOUT(character.status() != QQmlComponent::Loading, 3000);
        QVERIFY2(character.isReady(), qPrintable(character.errorString()));
        std::unique_ptr<QObject> characterConfig(character.create());
        QVERIFY2(characterConfig, qPrintable(character.errorString()));
        characterConfig->setProperty("cfg_modelPath", QStringLiteral("file:///tmp/character.model3.json"));
        QCOMPARE(characterConfig->property("cfg_modelPath").toString(),
                 QStringLiteral("file:///tmp/character.model3.json"));
        Live2DItem item;
        item.setInputMode(QStringLiteral("global"));
        item.setInputMode(QStringLiteral("unknown"));
        QCOMPARE(item.inputMode(), QStringLiteral("global"));
    }

    void desktopClickThrough()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(
            R"(
import QtQuick
Item {
    id: scene
    width: 240; height: 120
    property bool passthrough: false
    property bool editing: false
    property int desktopClicks: 0
    property int widgetClicks: 0
    property int siblingClicks: 0
    MouseArea { anchors.fill: parent; onClicked: ++desktopClicks }
    Item {
        id: wrapper
        width: 120; height: 120
        property Item applet: character
        MouseArea { anchors.fill: parent; onClicked: ++widgetClicks }
        Item { id: character; anchors.fill: parent }
    }
    MouseArea {
        x: 120; width: 120; height: 120
        onClicked: ++siblingClicks
    }
    DesktopInputGate {
        objectName: "gate"
        targetItem: character
        active: scene.passthrough
        editing: scene.editing
    }
}
)",
            QUrl::fromLocalFile(QStringLiteral(LIVE2D_CONFIG_QML)).resolved(QUrl("DesktopInputTest.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 3000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> instance(component.create());
        QVERIFY2(instance, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(instance.get());
        QVERIFY(item);
        QQuickWindow window;
        window.resize(240, 120);
        item->setParentItem(window.contentItem());
        window.show();
        QTest::qWait(30);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, {60, 60});
        QCOMPARE(item->property("widgetClicks").toInt(), 1);
        item->setProperty("passthrough", true);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, {60, 60});
        QCOMPARE(item->property("desktopClicks").toInt(), 1);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, {180, 60});
        QCOMPARE(item->property("siblingClicks").toInt(), 1);
        item->setProperty("editing", true);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, {60, 60});
        QCOMPARE(item->property("widgetClicks").toInt(), 2);
        item->setProperty("editing", false);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, {60, 60});
        QCOMPARE(item->property("desktopClicks").toInt(), 2);
        item->setProperty("passthrough", false);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, {60, 60});
        QCOMPARE(item->property("widgetClicks").toInt(), 3);
        // Removing the gate must restore the applet container too.
        item->setProperty("passthrough", true);
        delete item->findChild<QObject *>(QStringLiteral("gate"));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, {60, 60});
        QCOMPARE(item->property("widgetClicks").toInt(), 4);
    }

    void receiverLifecycle()
    {
        // Exercise the real receiver without connecting to the user's desktop bus.
        DesktopTracker tracker(QDBusConnection(QStringLiteral("live2d-test-disconnected")));
        const QString snapshot = QStringLiteral(R"({"x":-120,"y":350,"normal":true,"fullscreen":false})");
        QVERIFY(!tracker.update(snapshot));
        auto *first = new QObject;
        QObject second;
        tracker.subscribe(first);
        tracker.subscribe(first);
        tracker.subscribe(&second);
        QVERIFY(tracker.update(snapshot));
        QCOMPARE(tracker.state().position, QPointF(-120, 350));
        QVERIFY(tracker.state().normalWindow);
        QVERIFY(!tracker.state().fullscreen);
        QVERIFY(tracker.errorString().isEmpty());
        for (const auto &invalid :
             {QStringLiteral("invalid"), QStringLiteral(R"({"x":0})"),
              QStringLiteral(R"({"x":"0","y":0,"normal":true,"fullscreen":false})"), QString(1100, ' ')})
            QVERIFY(!tracker.update(invalid));
        delete first;
        QVERIFY(tracker.update(snapshot)); // Remaining item keeps the shared receiver active.
        tracker.unsubscribe(&second);
        QVERIFY(!tracker.state().valid);
        QVERIFY(!tracker.update(snapshot));
    }

    void kwinScript()
    {
        // Execute the shipped script with the same JS engine KWin uses, replacing
        // only its external workspace/timer/D-Bus objects with controllable fakes.
        QJSEngine engine;
        const auto setup = engine.evaluate(QStringLiteral(R"(
            var ticks = null;
            var activeTimer;
            function QTimer() {
                activeTimer = this;
                this.timeout = {connect: function (callback) { ticks = callback; }};
                this.start = function () { this.running = true; };
                this.stop = function () { this.running = false; };
            }
            var workspace = {cursorPos: {x: -100, y: 200}, activeWindow: null};
            var snapshots = [];
            var ack = null;
            var unloads = 0;
            var callbacks = [];
            function callDBus(service, path, iface, method, value, callback) {
                if (method === 'update') { snapshots.push(JSON.parse(value)); callbacks.push(callback); ack = callback; }
                else if (method === 'unloadScript') ++unloads;
            }
        )"));
        QVERIFY2(!setup.isError(), qPrintable(setup.toString()));
        QFile file(QStringLiteral(LIVE2D_CURSOR_SCRIPT));
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto script = file.readAll();
        script.replace("@RECEIVER@", "\":1.23\"");
        script.replace("@PLUGIN@", "\"live2d-test\"");
        const auto result = engine.evaluate(QString::fromUtf8(script));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        const auto run = [&](const QString &code) { return engine.evaluate(code); };
        QVERIFY(run(QStringLiteral("activeTimer.running")).toBool());
        QCOMPARE(run(QStringLiteral("activeTimer.interval")).toInt(), 8);
        run(QStringLiteral("ticks()"));
        QCOMPARE(run(QStringLiteral("snapshots[0].x")).toInt(), -100);
        QVERIFY(!run(QStringLiteral("snapshots[0].normal")).toBool());
        run(QStringLiteral("workspace.cursorPos.x = 50; ticks(); ticks()"));
        QCOMPARE(run(QStringLiteral("snapshots.length")).toInt(), 1); // No queued IPC flood.
        run(QStringLiteral(
            "ack(true); workspace.activeWindow = {normalWindow: true, fullScreen: true}; ticks()"));
        QCOMPARE(run(QStringLiteral("snapshots.length")).toInt(), 2);
        QVERIFY(run(QStringLiteral("snapshots[1].normal && snapshots[1].fullscreen")).toBool());
        QVERIFY(run(QStringLiteral("callbacks[0] === callbacks[1]")).toBool());
        // An unchanged poll must not build a JSON snapshot (or call stringify).
        run(QStringLiteral("ack(true); JSON.stringify = function () { throw new Error('unexpected "
                           "stringify'); }; for (var i = 0; i < 124; ++i) ticks()"));
        QCOMPARE(run(QStringLiteral("snapshots.length")).toInt(), 2);
        run(QStringLiteral("ticks()"));
        QCOMPARE(run(QStringLiteral("snapshots.length")).toInt(), 3); // Stationary heartbeat.
        run(QStringLiteral("ack(false)"));
        QVERIFY(!run(QStringLiteral("activeTimer.running")).toBool());
        QCOMPARE(run(QStringLiteral("unloads")).toInt(), 1);
    }

    void orphanScript()
    {
        // A vanished host must not leave a poller inside KWin indefinitely.
        QJSEngine engine;
        engine.evaluate(QStringLiteral(R"(
            var tick, stopped = false, unloaded = false;
            function QTimer() {
                this.timeout = {connect: function (f) { tick = f; }};
                this.start = function () {};
                this.stop = function () { stopped = true; };
            }
            var workspace = {cursorPos: {x: 0, y: 0}, activeWindow: null};
            function callDBus(service, path, iface, method) {
                if (method === 'unloadScript') unloaded = true;
            }
        )"));
        QFile file(QStringLiteral(LIVE2D_CURSOR_SCRIPT));
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto script = file.readAll();
        script.replace("@RECEIVER@", "\":1.23\"");
        script.replace("@PLUGIN@", "\"live2d-test\"");
        const auto result = engine.evaluate(QString::fromUtf8(script));
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
        engine.evaluate(QStringLiteral("for (var i = 0; i < 377; ++i) tick()"));
        QVERIFY(engine.evaluate(QStringLiteral("stopped && unloaded")).toBool());
    }
};
QTEST_MAIN(CursorTest)
#include "cursor_test.moc"
