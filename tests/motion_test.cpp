#include "motionpartopacity.h"
#include <CubismFramework.hpp>
#include <Id/CubismIdManager.hpp>
#include <Model/CubismMoc.hpp>
#include <Model/CubismModel.hpp>
#include <Motion/CubismMotion.hpp>
#include <Motion/CubismMotionManager.hpp>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <algorithm>
#include <cstdlib>

namespace
{
class Allocator final : public Csm::ICubismAllocator
{
  public:
    void *Allocate(Csm::csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void *memory) override { std::free(memory); }
    void *AllocateAligned(Csm::csmSizeType size, Csm::csmUint32 alignment) override
    {
        void *memory = nullptr;
        return posix_memalign(&memory, std::max<size_t>(alignment, sizeof(void *)), size) == 0 ? memory
                                                                                               : nullptr;
    }
    void DeallocateAligned(void *memory) override { std::free(memory); }
};
} // namespace

class MotionTest : public QObject
{
    Q_OBJECT
  private Q_SLOTS:
    void partOpacityCurves()
    {
        Allocator allocator;
        QVERIFY(Csm::CubismFramework::StartUp(&allocator));
        Csm::CubismFramework::Initialize();
        const auto cleanup = qScopeGuard([] {
            Csm::CubismFramework::Dispose();
            Csm::CubismFramework::CleanUp();
        });
        QFile file(QStringLiteral(LIVE2D_TEST_MOC));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto data = file.readAll();
        auto *moc =
            Csm::CubismMoc::Create(reinterpret_cast<const Csm::csmByte *>(data.constData()), data.size());
        QVERIFY(moc);
        const auto releaseMoc = qScopeGuard([&] { Csm::CubismMoc::Delete(moc); });
        auto *model = moc->CreateModel();
        QVERIFY(model);
        const auto releaseModel = qScopeGuard([&] { moc->DeleteModel(model); });
        QVERIFY(model->GetPartCount() > 0);
        const auto id = model->GetPartId(0);
        // A linear PartOpacity curve must change the actual part, including the
        // first update with delta=0 (loading a paused desktop widget).
        const QJsonObject motionJson{
            {"Version", 3},
            {"Meta", QJsonObject{{"Duration", 1.0},
                                 {"Fps", 30},
                                 {"Loop", false},
                                 {"AreBeziersRestricted", true},
                                 {"CurveCount", 1},
                                 {"TotalSegmentCount", 1},
                                 {"TotalPointCount", 2}}},
            {"Curves", QJsonArray{QJsonObject{{"Target", "PartOpacity"},
                                              {"Id", QString::fromUtf8(id->GetString().GetRawString())},
                                              {"Segments", QJsonArray{0, 0, 0, 1, 1}}}}}};
        const auto json = QJsonDocument(motionJson).toJson(QJsonDocument::Indented);
        auto *motion =
            Csm::CubismMotion::Create(reinterpret_cast<const Csm::csmByte *>(json.constData()), json.size());
        QVERIFY(motion);
        Csm::CubismMotionManager manager;
        MotionPartOpacity partOpacity;
        partOpacity.addMotion(model, json);
        manager.StartMotionPriority(motion, true, 1);
        model->SetPartOpacity(id, 1);
        QVERIFY(manager.UpdateMotion(model, 0));
        partOpacity.apply(model);
        QCOMPARE(model->GetPartOpacity(id), 0.0f);
        QVERIFY(manager.UpdateMotion(model, 0.5f));
        partOpacity.apply(model);
        QVERIFY(qAbs(model->GetPartOpacity(id) - 0.5f) < 0.001f);
        QVERIFY(manager.UpdateMotion(model, 0));
        partOpacity.apply(model);
        QVERIFY(qAbs(model->GetPartOpacity(id) - 0.5f) < 0.001f);
    }
};

QTEST_GUILESS_MAIN(MotionTest)
#include "motion_test.moc"
