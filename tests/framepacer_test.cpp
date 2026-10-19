#include "framepacer.h"
#include <QtTest>

class FramePacerTest : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void fractionalCadence_data()
    {
        QTest::addColumn<int>("rate");
        QTest::newRow("30 fps on 120 Hz") << 30;
        QTest::newRow("60 fps on 120 Hz") << 60;
    }

    void fractionalCadence()
    {
        QFETCH(int, rate);
        FramePacer pacer;
        qint64 now = 0;
        qint64 previousRefresh = -1;
        for (int frame = 0; frame < rate * 20; ++frame) {
            // Model the millisecond timer rounding and a 2 ms render/readback,
            // then consume at the next 120 Hz display refresh. A deadline
            // rebased on each wakeup periodically slips an extra refresh.
            const qint64 due = now + pacer.remaining(now, rate);
            now = (due + 999999) / 1000000 * 1000000;
            pacer.requested(now, rate);
            const qint64 refresh = ((now + 2000000) * 120 + 999999999) / 1000000000;
            if (previousRefresh >= 0)
                QCOMPARE(refresh - previousRefresh, qint64(120 / rate));
            previousRefresh = refresh;
            now = refresh * 1000000000 / 120;
        }
        QCOMPARE(previousRefresh, qint64(20 * 120 - 120 / rate + 1));
    }

    void inputAndMissedDeadlines()
    {
        FramePacer pacer;
        pacer.requested(0, 30);
        QCOMPARE(pacer.remaining(10000000, 30), qint64(23333333));
        pacer.requested(10000000, 30); // Immediate action keeps the periodic phase.
        QCOMPARE(pacer.remaining(10000000, 30), qint64(23333333));
        pacer.requested(33333333, 30); // The truncated nanosecond boundary advances.
        QCOMPARE(pacer.remaining(33333333, 30), qint64(33333333));
        pacer.requested(250000000, 30); // A stall skips elapsed slots.
        QCOMPARE(pacer.remaining(250000000, 30), qint64(16666666));
        pacer.reset(); // Pause, hide, reload and FPS changes reset the phase.
        QCOMPARE(pacer.remaining(90000000000000, 60), qint64(0));
        pacer.requested(90000000000000, 60);
        QCOMPARE(pacer.remaining(90000000000000, 60), qint64(16666666));
        pacer.requested(90000000000000, 0);
        QCOMPARE(pacer.remaining(90000000000000, 0), qint64(0));
    }
};
QTEST_GUILESS_MAIN(FramePacerTest)
#include "framepacer_test.moc"
