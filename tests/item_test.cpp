#include "globalinput.h"
#include "live2ditem.h"
#include "renderworker.h"
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

class ItemTest : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void stalledTransportDoesNotBlockGui()
    {
        QQuickWindow window;
        window.setGeometry(0, 0, 300, 420);
        auto *item = new Live2DItem(window.contentItem());
        item->setSize(QSizeF(300, 420));
        item->setFollowCursor(false);
        item->setRunning(false);
        item->setRenderBackend(QStringLiteral("opengl"));
        const auto model = QUrl::fromLocalFile(QString::fromLocal8Bit(qgetenv("LIVE2D_TEST_MODEL")));
        item->setSource(model);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);

        auto *worker = item->m_worker;
        QVERIFY(worker->thread() != QThread::currentThread());
        struct Gate {
            QSemaphore entered, release;
        };
        auto gate = std::make_shared<Gate>();
        auto unblock = qScopeGuard([gate] { gate->release.release(2); });
        const auto generation = item->m_generation;
        QMetaObject::invokeMethod(
            worker,
            [worker, gate, generation] {
                gate->entered.release();
                gate->release.acquire();
                // Completion and failure from the old model arrive after reload.
                QImage obsolete(8, 8, QImage::Format_RGBA8888_Premultiplied);
                obsolete.fill(Qt::red);
                Q_EMIT worker->frameReady(generation, obsolete, {});
                Q_EMIT worker->failed(generation, QStringLiteral("obsolete error"));
            },
            Qt::QueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), 1000);
        gate->entered.acquire();
        int ticks = 0;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; });
        heartbeat.start(10);
        QElapsedTimer elapsed;
        elapsed.start();
        item->reload();
        QVERIFY(elapsed.elapsed() < 100);
        QTest::qWait(100);
        QVERIFY(ticks >= 3);
        QVERIFY(!item->ready());
        gate->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        QVERIFY(item->errorString().isEmpty());
        QCOMPARE(item->m_frame.size(), QSize(300, 420));

        QPointer<QThread> thread = worker->thread();
        QPointer<RenderWorker> workerGuard = worker;
        QMetaObject::invokeMethod(
            worker,
            [gate] {
                gate->entered.release();
                gate->release.acquire();
            },
            Qt::QueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), 1000);
        elapsed.restart();
        delete item;
        QVERIFY(elapsed.elapsed() < 100);
        gate->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(workerGuard.isNull(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(thread.isNull(), 3000);
    }

    void presentationBackpressure()
    {
        // Deliberately stop consuming frames, as when Plasma's render thread
        // falls behind. Neither a fast timer nor unlimited mode may keep
        // replacing images and flooding the GUI thread with IPC. One extra
        // helper response is allowed so rendering overlaps presentation.
        QQuickRenderControl control;
        QQuickWindow window(&control);
        window.setGeometry(0, 0, 300, 420);
        QImage target(300, 420, QImage::Format_ARGB32_Premultiplied);
        window.setRenderTarget(QQuickRenderTarget::fromPaintDevice(&target));
        auto *item = new Live2DItem(window.contentItem());
        item->setSize(QSizeF(300, 420));
        item->setFollowCursor(false);
        item->setFrameRate(0);
        item->setRenderBackend(QStringLiteral("opengl"));
        item->setSource(QUrl::fromLocalFile(QString::fromLocal8Bit(qgetenv("LIVE2D_TEST_MODEL"))));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        QCOMPARE(reinterpret_cast<quintptr>(item->m_frame.constBits()) % alignof(quint32), quintptr(0));
        QVERIFY(item->m_frameDirty);
        // Exactly one prefetched response may finish while presentation stalls.
        QTRY_VERIFY_WITH_TIMEOUT(item->m_responseReady, 3000);
        QVERIFY(item->m_inFlight);
        QVERIFY(!item->m_timer.isActive());
        const auto queuedHeader = item->m_header;
        const auto key = item->m_frame.cacheKey();
        QTest::qWait(150);
        QCOMPARE(item->m_frame.cacheKey(), key);
        item->setFrameRate(240);
        item->setZoom(1.4);
        item->setZoom(1.5);
        QTest::qWait(150);
        QCOMPARE(item->m_frame.cacheKey(), key);
        QVERIFY(item->m_requestPending);
        QVERIFY(item->m_inFlight);
        QCOMPARE(item->m_header, queuedHeader);
        QVERIFY(!item->m_timer.isActive());
        control.polishItems();
        control.sync();
        control.render();
        QTRY_VERIFY_WITH_TIMEOUT(item->m_frame.cacheKey() != key, 3000);
        QVERIFY(item->m_frameDirty);
        QTRY_VERIFY_WITH_TIMEOUT(item->m_responseReady, 3000);
        // Frame storage must stay valid across later requests and teardown.
        const QImage retained = item->m_frame;
        const QImage expected = retained.copy();
        control.polishItems();
        control.sync();
        control.render();
        QTRY_VERIFY_WITH_TIMEOUT(item->m_frame.cacheKey() != retained.cacheKey(), 3000);
        QCOMPARE(retained, expected);
        delete item;
        control.invalidate();
        QCOMPARE(retained, expected);
    }

    void lockedCursorRelativeMotion()
    {
        QTemporaryDir dir;
        const QFileInfo original(QString::fromLocal8Bit(qgetenv("LIVE2D_TEST_MODEL")));
        QDirIterator files(original.path(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const auto path = files.next();
            const auto relative = QDir(original.path()).relativeFilePath(path);
            if (files.fileInfo().isDir())
                QVERIFY(QDir(dir.path()).mkpath(relative));
            else
                QVERIFY(QFile::copy(path, dir.filePath(relative)));
        }
        const auto write = [&](const QString &name, const QJsonObject &json) {
            QFile file(dir.filePath(name));
            if (!file.open(QIODevice::WriteOnly))
                return false;
            return file.write(QJsonDocument(json).toJson()) > 0;
        };
        QFile file(original.filePath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto settings = QJsonDocument::fromJson(file.readAll()).object();
        auto refs = settings["FileReferences"].toObject();
        for (const auto *name : {"Motions", "Physics", "Pose", "Expressions"})
            refs.remove(name);
        settings["FileReferences"] = refs;
        settings.remove("Groups");
        QVERIFY(write("relative.model3.json", settings));
        QJsonArray mappings;
        for (const auto *parameter : {"ParamAngleX", "ParamAngleY", "ParamEyeBallX", "ParamEyeBallY"}) {
            const double range = QString::fromLatin1(parameter) == QLatin1String("ParamAngleX") ? 30 : 0;
            mappings.append(QJsonObject{{"Input", "MousePositionX"},
                                        {"OutputLive2D", parameter},
                                        {"InputRangeLower", -1},
                                        {"InputRangeUpper", 1},
                                        {"OutputRangeLower", -range},
                                        {"OutputRangeUpper", range},
                                        {"Smoothing", 0}});
        }
        QVERIFY(
            write("relative.vtube.json", {{"FileReferences", QJsonObject{{"Model", "relative.model3.json"}}},
                                          {"ParameterSettings", mappings}}));
        QQuickWindow window;
        window.setColor(Qt::transparent);
        window.setGeometry(0, 0, 300, 420);
        auto *item = new Live2DItem(window.contentItem());
        item->setSize(QSizeF(300, 420));
        item->setMouseTrackingMode(QStringLiteral("relative"));
        item->setInputMode(QStringLiteral("off")); // Relative motion is independent of action keys.
        item->setCursorFollowMode(QStringLiteral("always"));
        item->setFrameRate(60);
        item->setRenderBackend(QStringLiteral("opengl"));
        item->setSource(QUrl::fromLocalFile(dir.filePath("relative.model3.json")));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        QTest::qWait(80);
        auto baseline = window.grabWindow();
        const auto distance = [](const QImage &reference, const QImage &image) {
            qint64 difference = 0;
            for (int y = 0; y < image.height() / 4; ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const auto a = reference.pixelColor(x, y), b = image.pixelColor(x, y);
                    difference +=
                        qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue());
                }
            return difference;
        };
        auto *input = GlobalInput::instance();
        const auto headDistance = [&](const QImage &image) { return distance(baseline, image); };
        // Emit the decoded evdev packet without ever changing the Qt/KWin
        // pointer position, reproducing the locked-cursor game's input path.
        input->pointerMoved({500, 0});
        QTRY_VERIFY_WITH_TIMEOUT(headDistance(window.grabWindow()) > 10000, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(item->m_pointer.x()) < .001, 2500);
        // Follow-off restores the model's own default parameters, which may
        // differ from a mapped zero. Verify the state, not identical poses.
        item->setFollowCursor(false);
        input->pointerMoved({500, 0});
        QTest::qWait(150);
        QCOMPARE(item->m_pointer, QPointF());
        QVERIFY(!item->m_relativeSubscribed);
        item->setFollowCursor(true);
        QVERIFY(item->m_relativeSubscribed);
        QTest::qWait(80);
        baseline = window.grabWindow();
        input->pointerMoved({-500, 0});
        QTRY_VERIFY_WITH_TIMEOUT(headDistance(window.grabWindow()) > 10000, 1000);
        item->setRunning(false);
        QVERIFY(!item->m_relativeSubscribed);
        QTest::qWait(100);
        const auto paused = window.grabWindow();
        input->pointerMoved({1000, 0});
        QTest::qWait(100);
        QCOMPARE(window.grabWindow(), paused);
        item->setRunning(true);
        QTRY_COMPARE_WITH_TIMEOUT(item->m_pointer, QPointF(), 1000);
        QTest::qWait(80);
        baseline = window.grabWindow();
        input->pointerMoved({500, 0});
        QTRY_VERIFY_WITH_TIMEOUT(headDistance(window.grabWindow()) > 10000, 1000);
        input->pointerReset();
        QTRY_COMPARE_WITH_TIMEOUT(item->m_pointer, QPointF(), 1000);
        window.hide();
        input->pointerMoved({500, 0});
        QTRY_VERIFY_WITH_TIMEOUT(!item->m_relativeSubscribed, 1000);
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(item->m_pointer, QPointF(), 1000);
        // Policies continue to gate relative movement while a foreground
        // window status is unavailable, rather than bypassing that policy.
        item->setCursorFollowMode(QStringLiteral("notFullscreen"));
        input->pointerMoved({500, 0});
        QTest::qWait(80);
        QCOMPARE(item->m_pointer, QPointF());
        item->setMouseTrackingMode(QStringLiteral("screen"));
        QVERIFY(!item->m_relativeSubscribed);
        delete item;
    }

    void focusedInputAndManualMotion()
    {
        QTemporaryDir dir;
        const QFileInfo original(QString::fromLocal8Bit(qgetenv("LIVE2D_TEST_MODEL")));
        QDirIterator files(original.path(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const auto path = files.next();
            const auto relative = QDir(original.path()).relativeFilePath(path);
            if (files.fileInfo().isDir())
                QVERIFY(QDir(dir.path()).mkpath(relative));
            else
                QVERIFY(QFile::copy(path, dir.filePath(relative)));
        }
        const auto write = [&](const QString &name, const QJsonObject &json) {
            QFile file(dir.filePath(name));
            if (!file.open(QIODevice::WriteOnly))
                return false;
            return file.write(QJsonDocument(json).toJson()) > 0;
        };
        QFile originalFile(original.filePath());
        QVERIFY(originalFile.open(QIODevice::ReadOnly));
        auto settings = QJsonDocument::fromJson(originalFile.readAll()).object();
        auto refs = settings["FileReferences"].toObject();
        refs.remove("Motions");
        refs.remove("Expressions");
        refs.remove("Physics");
        settings["FileReferences"] = refs;
        settings.remove("Groups"); // Keep automatic blinking out of the input comparison.
        QVERIFY(write("input.model3.json", settings));
        const QJsonObject expression{
            {"Type", "Live2D Expression"},
            {"Parameters", QJsonArray{QJsonObject{{"Id", "ParamAngleX"}, {"Value", 25}, {"Blend", "Add"}}}}};
        QVERIFY(write("held.exp3.json", expression));
        const QJsonObject motion{
            {"Version", 3},
            {"Meta", QJsonObject{{"Duration", 1},
                                 {"Fps", 30},
                                 {"Loop", false},
                                 {"CurveCount", 1},
                                 {"TotalSegmentCount", 1},
                                 {"TotalPointCount", 2}}},
            {"Curves", QJsonArray{QJsonObject{{"Target", "Parameter"},
                                              {"Id", "ParamAngleX"},
                                              {"Segments", QJsonArray{0, -25, 0, 1, -25}}}}}};
        QVERIFY(write("wave.motion3.json", motion));
        QVERIFY(write("input.vtube.json",
                      {{"FileReferences", QJsonObject{{"Model", "input.model3.json"}}},
                       {"Hotkeys", QJsonArray{QJsonObject{{"Action", "ToggleExpression"},
                                                          {"File", "held.exp3.json"},
                                                          {"DeactivateAfterKeyUp", true},
                                                          {"Triggers", QJsonObject{{"Trigger1", "W"}}}},
                                              QJsonObject{{"Action", "TriggerAnimation"},
                                                          {"File", "wave.motion3.json"},
                                                          {"FadeSecondsAmount", 0},
                                                          {"Triggers", QJsonObject{{"Trigger1", "A"}}}}}}}));
        QQuickWindow window;
        window.setGeometry(0, 0, 300, 420);
        auto *item = new Live2DItem(window.contentItem());
        item->setSize(QSizeF(300, 420));
        item->setFollowCursor(false);
        item->setFrameRate(1); // Explicit input must render without waiting for the periodic timer.
        item->setRenderBackend(QStringLiteral("opengl"));
        item->setSource(QUrl::fromLocalFile(dir.filePath("input.model3.json")));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        QCOMPARE(item->motionGroups(), QStringList{"wave"});
        item->forceActiveFocus();
        QTRY_VERIFY(item->hasActiveFocus());
        QTest::qWait(100); // ready() precedes presentation of the first frame.
        auto first = window.grabWindow();
        const auto distance = [](const QImage &a, const QImage &b) {
            // AngleX turns Haru's head. Exclude the breathing torso and pose
            // fades, which legitimately keep advancing between input frames.
            qint64 difference = 0;
            for (int y = 0; y < a.height() / 4; ++y)
                for (int x = 0; x < a.width(); ++x) {
                    const auto ca = a.pixelColor(x, y), cb = b.pixelColor(x, y);
                    difference += qAbs(ca.red() - cb.red()) + qAbs(ca.green() - cb.green()) +
                                  qAbs(ca.blue() - cb.blue());
                }
            return difference;
        };
        QTest::keyPress(&window, Qt::Key_W);
        QTRY_VERIFY_WITH_TIMEOUT(distance(window.grabWindow(), first) > 10000, 3000);
        auto pressedDifference = distance(window.grabWindow(), first);
        QVERIFY(pressedDifference > 10000);
        QTest::keyRelease(&window, Qt::Key_W);
        // Breathing can still advance slightly between input requests.
        QTRY_VERIFY_WITH_TIMEOUT(distance(window.grabWindow(), first) < pressedDifference / 4, 3000);
        item->setInputMode(QStringLiteral("off"));
        QTest::keyClick(&window, Qt::Key_W);
        QTest::qWait(150);
        QVERIFY(distance(window.grabWindow(), first) < pressedDifference / 4);
        first = window.grabWindow(); // Compare each gesture with the current breathing pose.
        item->setInputMode(QStringLiteral("focused"));
        QTest::keyPress(&window, Qt::Key_W);
        QTRY_VERIFY_WITH_TIMEOUT(distance(window.grabWindow(), first) > pressedDifference / 2, 3000);
        pressedDifference = distance(window.grabWindow(), first);
        item->setFocus(false); // Losing focus releases held expressions.
        QVERIFY(!item->hasActiveFocus());
        QTRY_VERIFY_WITH_TIMEOUT(distance(window.grabWindow(), first) < pressedDifference / 4, 3000);
        item->playMotion(QStringLiteral("wave"));
        QTRY_VERIFY_WITH_TIMEOUT(distance(window.grabWindow(), first) > pressedDifference / 2, 3000);
        delete item;
    }

    void unlimitedFrameRate()
    {
        QQuickWindow window;
        window.setColor(Qt::transparent);
        window.setGeometry(0, 0, 300, 420);
        auto *item = new Live2DItem(window.contentItem());
        item->setSize(QSizeF(300, 420));
        item->setRenderBackend(QStringLiteral("opengl"));
        item->setFollowCursor(false);
        item->setFrameRate(0);
        QCOMPARE(item->frameRate(), 0);
        item->setRunning(false);
        item->setSource(QUrl::fromLocalFile(QString::fromLocal8Bit(qgetenv("LIVE2D_TEST_MODEL"))));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        QTest::qWait(100);
        auto image = window.grabWindow();
        QVERIFY(!image.isNull());
        QTest::qWait(150);
        QCOMPARE(window.grabWindow(), image);
        item->setRunning(true);
        QTRY_VERIFY_WITH_TIMEOUT(window.grabWindow() != image, 3000);
        item->setRunning(false);
        QTest::qWait(200); // Allow the single request in flight to finish.
        image = window.grabWindow();
        QTest::qWait(150);
        QCOMPARE(window.grabWindow(), image);
        item->setFrameRate(144);
        QCOMPARE(item->frameRate(), 144);
        item->setRunning(true);
        QTRY_VERIFY_WITH_TIMEOUT(window.grabWindow() != image, 3000);
        item->setFrameRate(0); // Switching from a running fixed-rate timer starts the chain.
        image = window.grabWindow();
        QTRY_VERIFY_WITH_TIMEOUT(window.grabWindow() != image, 3000);
        window.hide();
        item->reload();
        QTest::qWait(100);
        QVERIFY(!item->ready());
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        image = window.grabWindow();
        QTRY_VERIFY_WITH_TIMEOUT(window.grabWindow() != image, 3000);
        delete item;
    }

    void lifecycle()
    {
        QQuickWindow window;
        window.setColor(Qt::transparent);
        window.setGeometry(0, 0, 300, 420);
        auto *item = new Live2DItem(window.contentItem());
        item->setSize(QSizeF(300, 420));
        item->setRenderBackend(QStringLiteral("opengl"));
        const auto model = QUrl::fromLocalFile(QString::fromLocal8Bit(qgetenv("LIVE2D_TEST_MODEL")));
        item->setSource(model);
        item->setRunning(false); // Loading still renders one frame while paused.
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        QCOMPARE(item->activeBackend(), QStringLiteral("opengl"));
        QTest::qWait(100);
        const auto first = window.grabWindow();
        QVERIFY(!first.isNull());
        QTest::qWait(150);
        QCOMPARE(window.grabWindow(), first);
        item->setZoom(1.3);
        item->setZoom(1.5); // Coalesce changes made while a frame is in flight.
        QTRY_VERIFY_WITH_TIMEOUT(window.grabWindow() != first, 3000);
        window.hide();
        item->reload();
        QTest::qWait(100);
        QVERIFY(!item->ready());
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        item->setRunning(true);
        item->setGaze(.2, .3);
        item->tap(.5, .65);
        if (!item->expressions().isEmpty()) {
            item->setExpression(item->expressions().first());
        }
        item->setSource(QUrl::fromLocalFile(QStringLiteral("/missing/character.model3.json")));
        QTRY_VERIFY_WITH_TIMEOUT(!item->errorString().isEmpty(), 15000);
        QVERIFY(!item->ready());
        QVERIFY(item->activeBackend().isEmpty());
        item->setSource(model);
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        // Even rapid source changes must not let an older response overwrite status.
        item->setSource(QUrl::fromLocalFile(QStringLiteral("/missing/character.model3.json")));
        item->setSource(model);
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        delete item; // Terminate a helper while animation requests may be pending.
    }

    void fallbackAndExplicitFailure()
    {
        const auto previous = qgetenv("VK_DRIVER_FILES");
        const bool wasSet = qEnvironmentVariableIsSet("VK_DRIVER_FILES");
        qputenv("VK_DRIVER_FILES", "/nonexistent/live2d-vulkan-icd.json");
        const auto restore = qScopeGuard([&] {
            if (wasSet) {
                qputenv("VK_DRIVER_FILES", previous);
            } else {
                qunsetenv("VK_DRIVER_FILES");
            }
        });
        QQuickWindow window;
        window.setGeometry(0, 0, 300, 420);
        auto *item = new Live2DItem(window.contentItem());
        item->setSize(QSizeF(300, 420));
        item->setSource(QUrl::fromLocalFile(QString::fromLocal8Bit(qgetenv("LIVE2D_TEST_MODEL"))));
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        QCOMPARE(item->renderBackend(), QStringLiteral("auto"));
        QCOMPARE(item->activeBackend(), QStringLiteral("opengl"));
        item->setRenderBackend(QStringLiteral("vulkan"));
        QTRY_VERIFY_WITH_TIMEOUT(!item->errorString().isEmpty(), 15000);
        QVERIFY(!item->ready());
        QVERIFY(item->activeBackend().isEmpty());
        item->setRenderBackend(QStringLiteral("opengl"));
        QTRY_VERIFY_WITH_TIMEOUT(item->ready(), 15000);
        item->setRenderBackend(QStringLiteral("unknown"));
        QCOMPARE(item->renderBackend(), QStringLiteral("opengl"));
    }
};
QTEST_MAIN(ItemTest)
#include "item_test.moc"
