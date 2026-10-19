#include "live2ditem.h"
#include "live2dwindow.h"
#include <LayerShellQt/Window>
#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>
#include <limits>

class WindowTest : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void placementAndClose()
    {
        QQuickWindow desktop;
        desktop.setGeometry(0, 0, 600, 600);
        desktop.show();
        QQuickItem anchor(desktop.contentItem());
        anchor.setPosition({80, 100});
        const auto initial = anchor.mapToGlobal(QPointF()).toPoint() - desktop.screen()->geometry().topLeft();
        Live2DWindow overlay;
        overlay.resize(200, 250);
        overlay.setAnchorItem(&anchor);
        overlay.showAtAnchor();
        QCOMPARE(overlay.color(), QColor(Qt::transparent));
        QCOMPARE(overlay.transientParent(), nullptr);
        auto *layer = LayerShellQt::Window::get(&overlay);
        QCOMPARE(layer->layer(), LayerShellQt::Window::LayerOverlay);
        QCOMPARE(layer->exclusionZone(), -1);
        QCOMPARE(layer->keyboardInteractivity(), LayerShellQt::Window::KeyboardInteractivityOnDemand);
        QVERIFY(!layer->activateOnShow());
        QSignalSpy inputChanged(&overlay, &Live2DWindow::clickThroughChanged);
        const auto position = overlay.position();
        overlay.setClickThrough(true);
        overlay.setClickThrough(true);
        QCOMPARE(inputChanged.size(), 1);
        QVERIFY(overlay.flags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(overlay.flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QCOMPARE(layer->keyboardInteractivity(), LayerShellQt::Window::KeyboardInteractivityNone);
        QVERIFY(overlay.isVisible());
        QCOMPARE(overlay.position(), position);
        overlay.setClickThrough(false);
        QCOMPARE(inputChanged.size(), 2);
        QVERIFY(!overlay.flags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(!overlay.flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QCOMPARE(layer->keyboardInteractivity(), LayerShellQt::Window::KeyboardInteractivityOnDemand);
        QCOMPARE(layer->margins().left(), initial.x());
        QCOMPARE(layer->margins().top(), initial.y());
        overlay.beginMove();
        overlay.moveFromStart({15, 20});
        QCOMPARE(layer->margins().left(), initial.x() + 15);
        QCOMPARE(layer->margins().top(), initial.y() + 20);
        overlay.moveFromStart({25, 30}); // Translations are from the gesture's start.
        QCOMPARE(layer->margins().left(), initial.x() + 25);
        QCOMPARE(layer->margins().top(), initial.y() + 30);
        overlay.moveFromStart({-10000, -10000});
        QCOMPARE(layer->margins(), QMargins());
        overlay.moveFromStart({std::numeric_limits<double>::quiet_NaN(), 1});
        QCOMPARE(layer->margins(), QMargins());
        QSignalSpy close(&overlay, &Live2DWindow::returnToDesktop);
        QVERIFY(!overlay.close()); // Closing returns the model to its applet.
        QTRY_COMPARE(close.size(), 1);
        QVERIFY(overlay.isVisible());
    }

    void qmlLifecycle()
    {
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(LIVE2D_TEST_QML_DIR));
        engine.rootContext()->setContextProperty(
            QStringLiteral("i18n"),
            QVariant::fromValue(engine.evaluate(QStringLiteral("(function (text) { return text; })"))));
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(LIVE2D_FLOATING_QML)));
        QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 3000);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        for (int cycle = 0; cycle < 2; ++cycle) {
            std::unique_ptr<QObject> instance(component.create());
            QVERIFY2(instance, qPrintable(component.errorString()));
            auto *overlay = qobject_cast<Live2DWindow *>(instance.get());
            QVERIFY(overlay);
            auto *model = overlay->findChild<Live2DItem *>();
            QVERIFY(model);
            overlay->resize(300, 420);
            auto config = engine.newObject();
            config.setProperty(
                "modelPath",
                QUrl::fromLocalFile(QString::fromLocal8Bit(qgetenv("LIVE2D_TEST_MODEL"))).toString());
            config.setProperty("renderBackend", QStringLiteral("opengl"));
            config.setProperty("running", true);
            config.setProperty("frameRate", 30);
            config.setProperty("zoom", 1);
            config.setProperty("cursorFollowMode", QStringLiteral("off"));
            config.setProperty("mouseTrackingMode", QStringLiteral("relative"));
            config.setProperty("relativeSensitivity", 2.5);
            config.setProperty("idleGroup", QStringLiteral("Idle"));
            config.setProperty("inputMode", QStringLiteral("focused"));
            config.setProperty("clickThrough", false);
            QVERIFY(overlay->setProperty("configuration", QVariant::fromValue(config)));
            overlay->showAtAnchor();
            QTRY_VERIFY_WITH_TIMEOUT(model->ready(), 15000);
            QCOMPARE(model->mouseTrackingMode(), QStringLiteral("relative"));
            QCOMPARE(model->relativeSensitivity(), 2.5);
            const auto image = overlay->grabWindow();
            QVERIFY(!image.isNull());
            int opaque = 0, transparent = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    opaque += image.pixelColor(x, y).alpha() > 128;
                    transparent += image.pixelColor(x, y).alpha() == 0;
                }
            QVERIFY(opaque > 5000);
            QVERIFY(transparent > 5000);
            auto *handle = overlay->findChild<QQuickItem *>(QStringLiteral("floatingMoveButton"));
            QVERIFY(handle);
            const auto point = handle->mapToScene({handle->width() / 2, handle->height() / 2}).toPoint();
            QTest::mouseMove(overlay, point);
            QTest::qWait(30);
            QVERIFY(handle->isVisible());
            auto *layer = LayerShellQt::Window::get(overlay);
            const auto start = layer->margins();
            QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseMove(overlay, point - QPoint(20, 20));
            QTRY_COMPARE(layer->margins().left(), start.left() - 20);
            QTRY_COMPARE(layer->margins().top(), start.top() - 20);
            QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, point - QPoint(20, 20));
            // Replace the plain JS configuration to notify bindings, as the
            // real KConfig property change does. Re-enable after a live toggle.
            auto blockedConfig = engine.toScriptValue(config.toVariant());
            blockedConfig.setProperty("clickThrough", true);
            QVERIFY(overlay->setProperty("configuration", QVariant::fromValue(blockedConfig)));
            QTRY_VERIFY(overlay->clickThrough());
            QVERIFY(!model->property("localInputEnabled").toBool());
            QVERIFY(!handle->isVisible());
            QVERIFY(overlay->setProperty("configuration", QVariant::fromValue(config)));
            QTRY_VERIFY(!overlay->clickThrough());
            QVERIFY(model->property("localInputEnabled").toBool());
        } // Destroying the floating QML also destroys its helper and handlers.
    }
};

QTEST_MAIN(WindowTest)
#include "window_test.moc"
