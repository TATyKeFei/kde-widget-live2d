#include "globalinput.h"
#include "inputkeys.h"
#include "modelinputstate.h"
#include "relativepointer.h"
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

class GlobalInputTest : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void slowProbeDoesNotBlockGui()
    {
        GlobalInput input;
        QObject client;
        QSemaphore entered, release;
        QMetaObject::invokeMethod(
            input.m_probeWorker,
            [&] {
                entered.release();
                release.acquire();
            },
            Qt::QueuedConnection);
        const bool started = entered.tryAcquire(1, 2000);
        if (!started)
            release.release();
        QVERIFY(started);
        // Simulate a driver blocked in open/ioctl. Subscription and GUI timers
        // must remain responsive, and cancelled scans must not install devices.
        input.subscribe(&client, {QStringLiteral("W")}, true);
        bool heartbeat = false;
        QTimer::singleShot(0, &input, [&] { heartbeat = true; });
        QTest::qWait(30);
        const bool responsive = heartbeat && input.m_scanBusy;
        input.unsubscribe(&client);
        release.release();
        QVERIFY(responsive);
        QTRY_VERIFY(!input.m_scanBusy);
        QVERIFY(input.m_devices.empty());
        QVERIFY(input.m_clients.isEmpty());
    }

    void relativeMotionPackets()
    {
        GlobalInput input;
        QObject client;
        input.m_motionClients.insert(&client);
        GlobalInput::Device mouse;
        mouse.relative = true;
        QSignalSpy moves(&input, &GlobalInput::pointerMoved);
        QSignalSpy resets(&input, &GlobalInput::pointerReset);
        input.event(mouse, EV_REL, REL_X, 12);
        input.event(mouse, EV_REL, REL_Y, -7);
        input.event(mouse, EV_REL, REL_WHEEL, 1); // Wheel events never move the character.
        QVERIFY(moves.isEmpty());
        input.event(mouse, EV_SYN, SYN_REPORT, 0);
        QCOMPARE(moves.size(), 1);
        QCOMPARE(moves.last()[0].toPointF(), QPointF(12, -7));
        input.event(mouse, EV_SYN, SYN_REPORT, 0);
        QCOMPARE(moves.size(), 1); // No repeats for empty packets.
        input.event(mouse, EV_REL, REL_X, 100);
        input.event(mouse, EV_SYN, SYN_DROPPED, 0);
        input.event(mouse, EV_REL, REL_Y, 100);
        input.event(mouse, EV_SYN, SYN_REPORT, 0);
        QCOMPARE(moves.size(), 1); // Drop the entire lost packet, including its pending X.
        QCOMPARE(resets.size(), 1);
        input.event(mouse, EV_REL, REL_Y, 4);
        input.event(mouse, EV_SYN, SYN_REPORT, 0);
        QCOMPARE(moves.last()[0].toPointF(), QPointF(0, 4));
        input.event(mouse, EV_REL, REL_X, 8);
        input.releaseDevice(mouse);
        input.event(mouse, EV_SYN, SYN_REPORT, 0);
        QCOMPARE(moves.size(), 2);
        QCOMPARE(resets.size(), 2);
        input.m_motionClients.clear();
        input.event(mouse, EV_REL, REL_X, 10);
        input.event(mouse, EV_SYN, SYN_REPORT, 0);
        QCOMPARE(moves.size(), 2); // No opt-in subscriber, no motion.
    }

    void relativeNotifierAndVirtualPointer()
    {
        GlobalInput input;
        QObject client;
        input.m_motionClients.insert(&client);
        int descriptors[2];
        QCOMPARE(pipe2(descriptors, O_NONBLOCK | O_CLOEXEC), 0);
        const auto closeWriter = qScopeGuard([&] { ::close(descriptors[1]); });
        auto device = std::make_unique<GlobalInput::Device>();
        device->fd = descriptors[0];
        device->relative = true;
        device->notifier = std::make_unique<QSocketNotifier>(device->fd, QSocketNotifier::Read);
        connect(device->notifier.get(), &QSocketNotifier::activated, &input,
                [&] { input.readDevice(QStringLiteral("mouse-pipe")); });
        input.m_devices.emplace(QStringLiteral("mouse-pipe"), std::move(device));
        RelativePointer pointer;
        connect(&input, &GlobalInput::pointerMoved, &input,
                [&](const QPointF &delta) { pointer.move(delta, 1, 0); });
        QSignalSpy moves(&input, &GlobalInput::pointerMoved);
        input_event packet[3]{};
        packet[0].type = EV_REL;
        packet[0].code = REL_X;
        packet[0].value = 250;
        packet[1].type = EV_REL;
        packet[1].code = REL_Y;
        packet[1].value = -125;
        packet[2].type = EV_SYN;
        packet[2].code = SYN_REPORT;
        QCOMPARE(::write(descriptors[1], packet, sizeof(packet)), ssize_t(sizeof(packet)));
        QTRY_COMPARE(moves.size(), 1);
        QCOMPARE(pointer.position(.1), QPointF(.5, .25));
        const auto decayed = pointer.position(.37);
        QVERIFY(std::abs(decayed.x() - .5 / std::exp(1.0)) < 1e-12);
        RelativePointer manyFrames;
        manyFrames.move({250, -125}, 1, 0);
        for (int i = 1; i <= 37; ++i)
            manyFrames.position(i * .01);
        QVERIFY(std::abs(manyFrames.position(.37).x() - decayed.x()) < 1e-12);
        pointer.move({100000, -100000}, 1, .4);
        QCOMPARE(pointer.position(.4), QPointF(1, 1));
        pointer.move({-100000, 100000}, 1, .4);
        QCOMPARE(pointer.position(.4), QPointF(-1, -1));
        pointer.reset();
        QCOMPARE(pointer.position(.5), QPointF());
        pointer.move({250, 0}, 2, .5);
        QCOMPARE(pointer.position(.5), QPointF(1, 0));
        pointer.move({0, 0}, 2, .6); // Empty packets must not delay centering.
        QVERIFY(pointer.position(.8).x() < .5);
        pointer.reset();
        pointer.move({std::numeric_limits<double>::quiet_NaN(), 1}, 1, 1);
        QCOMPARE(pointer.position(1), QPointF());
    }
    void notifierReadsAndDisconnects()
    {
        GlobalInput input;
        input.m_keys = {"W"};
        int descriptors[2];
        QCOMPARE(pipe2(descriptors, O_NONBLOCK | O_CLOEXEC), 0);
        const auto closeWriter = qScopeGuard([&] { ::close(descriptors[1]); });
        auto device = std::make_unique<GlobalInput::Device>();
        device->fd = descriptors[0];
        device->notifier = std::make_unique<QSocketNotifier>(device->fd, QSocketNotifier::Read);
        connect(device->notifier.get(), &QSocketNotifier::activated, &input,
                [&] { input.readDevice(QStringLiteral("pipe")); });
        input.m_devices.emplace(QStringLiteral("pipe"), std::move(device));
        QSignalSpy events(&input, &GlobalInput::keyChanged);
        input_event down{};
        down.type = EV_KEY;
        down.code = KEY_W;
        down.value = 1;
        QCOMPARE(::write(descriptors[1], &down, sizeof(down)), ssize_t(sizeof(down)));
        QTRY_COMPARE(events.size(), 1);
        QVERIFY(events.last()[1].toBool());
        // Device loss while held emits a release, then removes its notifier.
        ::close(descriptors[1]);
        descriptors[1] = -1;
        QTRY_COMPARE(events.size(), 2);
        QVERIFY(!events.last()[1].toBool());
        QVERIFY(input.m_devices.empty());
    }

    void evdevEdgesAndDeviceRemoval()
    {
        GlobalInput input;
        input.m_keys = {"W", "LeftMouseButton"};
        GlobalInput::Device keyboard, secondKeyboard, mouse;
        QSignalSpy events(&input, &GlobalInput::keyChanged);
        input.event(keyboard, EV_KEY, KEY_W, 1);
        input.event(keyboard, EV_KEY, KEY_W, 2); // Repeat never retriggers an action.
        input.event(keyboard, EV_KEY, KEY_W, 1);
        input.event(keyboard, EV_KEY, KEY_A, 1); // Unconfigured keys are discarded.
        QCOMPARE(events.size(), 1);
        QCOMPARE(events[0][0].toString(), QStringLiteral("W"));
        QVERIFY(events[0][1].toBool());
        input.event(secondKeyboard, EV_KEY, KEY_W, 1);
        input.event(keyboard, EV_KEY, KEY_W, 0);
        QCOMPARE(events.size(), 1); // Still held on the second keyboard.
        input.releaseDevice(secondKeyboard);
        QCOMPARE(events.size(), 2);
        QVERIFY(!events.last()[1].toBool());
        input.event(mouse, EV_KEY, BTN_LEFT, 1);
        QCOMPARE(events.size(), 3);
        input.event(mouse, EV_SYN, SYN_DROPPED, 0);
        QCOMPARE(events.size(), 4); // Dropped events cannot leave a held expression stuck.
        QVERIFY(!events.last()[1].toBool());
        input.event(mouse, EV_KEY, BTN_LEFT, 1);
        QCOMPARE(events.size(), 4);
        input.event(mouse, EV_SYN, SYN_REPORT, 0);
        input.event(mouse, EV_KEY, BTN_LEFT, 1);
        input.releaseDevice(mouse);
        QCOMPARE(events.size(), 6);
    }

    void chordsTogglesAndRelease()
    {
        ModelInputState state;
        state.setBindings({QJsonObject{{"action", "motion"}, {"group", "W"}, {"keys", QJsonArray{"W"}}},
                           QJsonObject{{"action", "expression"},
                                       {"name", "mouse"},
                                       {"onRelease", true},
                                       {"keys", QJsonArray{"LeftMouseButton"}}},
                           QJsonObject{{"action", "expression"},
                                       {"name", "toggle"},
                                       {"keys", QJsonArray{"LeftControl", "N1"}}}});
        auto actions = state.key("W", true);
        QCOMPARE(actions.size(), 1);
        QCOMPARE(actions[0].toObject()["group"].toString(), QStringLiteral("W"));
        QVERIFY(state.key("W", true).isEmpty());
        QVERIFY(state.key("W", false).isEmpty());
        QCOMPARE(state.key("W", true).size(), 1);
        actions = state.key("LeftMouseButton", true);
        QVERIFY(actions[0].toObject()["active"].toBool());
        actions = state.key("LeftMouseButton", false);
        QVERIFY(!actions[0].toObject()["active"].toBool());
        QVERIFY(state.key("LeftControl", true).isEmpty());
        actions = state.key("N1", true);
        QVERIFY(actions[0].toObject()["active"].toBool());
        QVERIFY(state.key("N1", false).isEmpty());
        actions = state.key("N1", true);
        QVERIFY(!actions[0].toObject()["active"].toBool());
        state.key("LeftMouseButton", true);
        actions = state.clear();
        QCOMPARE(actions.size(), 1);
        QVERIFY(!actions[0].toObject()["active"].toBool());
        QVERIFY(state.clear().isEmpty());
    }

    void qtKeyMappingAndSubscribers()
    {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
        QCOMPARE(InputKeys::fromQt(&key), QStringLiteral("W"));
        QKeyEvent digit(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier);
        QCOMPARE(InputKeys::fromQt(&digit), QStringLiteral("N1"));
        // Same physical key with another layout still has the W binding.
        QKeyEvent physical(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, KEY_W + 8, 0, 0);
        QCOMPARE(InputKeys::fromQt(&physical), QStringLiteral("W"));
        GlobalInput input;
        auto *first = new QObject;
        QObject second;
        input.subscribe(first, {"W"});
        input.subscribe(&second, {"LeftMouseButton"});
        QVERIFY(input.m_rescan.isActive());
        QCOMPARE(input.m_keys.size(), 2);
        delete first;
        QCOMPARE(input.m_keys, QSet<QString>{"LeftMouseButton"});
        input.unsubscribe(&second);
        QVERIFY(!input.m_rescan.isActive());
        QVERIFY(input.m_devices.empty());
        QVERIFY(input.errorString().isEmpty());
        auto *motionOnly = new QObject;
        input.subscribe(motionOnly, {}, true);
        QVERIFY(input.m_keys.isEmpty());
        QVERIFY(input.m_motionClients.contains(motionOnly));
        input.subscribe(&second, {"W"});
        delete motionOnly;
        QVERIFY(input.m_motionClients.isEmpty());
        QCOMPARE(input.m_keys, QSet<QString>{"W"});
        QVERIFY(input.m_rescan.isActive());
        input.subscribe(&second, {}, true); // Switch one client from keys to motion.
        QVERIFY(input.m_keys.isEmpty());
        QVERIFY(input.m_motionClients.contains(&second));
        input.unsubscribe(&second);
        QVERIFY(!input.m_rescan.isActive());
        QVERIFY(input.motionErrorString().isEmpty());
    }
};

QTEST_GUILESS_MAIN(GlobalInputTest)
#include "globalinput_test.moc"
