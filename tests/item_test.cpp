#include "live2ditem.h"
#include <QQuickWindow>
#include <QtTest>

class ItemTest : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
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
