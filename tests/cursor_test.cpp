#include "cursorstate.h"
#include "desktoptracker.h"
#include "live2ditem.h"
#include <QFile>
#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
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
    }

    void itemModes()
    {
        Live2DItem item;
        QCOMPARE(item.cursorFollowMode(), QStringLiteral("always"));
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
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(LIVE2D_CONFIG_QML)));
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
            function callDBus(service, path, iface, method, value, callback) {
                if (method === 'update') { snapshots.push(JSON.parse(value)); ack = callback; }
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
        QCOMPARE(run(QStringLiteral("activeTimer.interval")).toInt(), 33);
        run(QStringLiteral("ticks()"));
        QCOMPARE(run(QStringLiteral("snapshots[0].x")).toInt(), -100);
        QVERIFY(!run(QStringLiteral("snapshots[0].normal")).toBool());
        run(QStringLiteral("workspace.cursorPos.x = 50; ticks(); ticks()"));
        QCOMPARE(run(QStringLiteral("snapshots.length")).toInt(), 1); // No queued IPC flood.
        run(QStringLiteral(
            "ack(true); workspace.activeWindow = {normalWindow: true, fullScreen: true}; ticks()"));
        QCOMPARE(run(QStringLiteral("snapshots.length")).toInt(), 2);
        QVERIFY(run(QStringLiteral("snapshots[1].normal && snapshots[1].fullscreen")).toBool());
        run(QStringLiteral("ack(true); for (var i = 0; i < 30; ++i) ticks()"));
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
        engine.evaluate(QStringLiteral("for (var i = 0; i < 92; ++i) tick()"));
        QVERIFY(engine.evaluate(QStringLiteral("stopped && unloaded")).toBool());
    }
};
QTEST_MAIN(CursorTest)
#include "cursor_test.moc"
