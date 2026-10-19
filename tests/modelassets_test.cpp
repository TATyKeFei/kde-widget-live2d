#include "modelassets.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

class ModelAssetsTest : public QObject
{
    Q_OBJECT
  private:
    static void write(const QString &path, const QByteArray &data)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(data), data.size());
    }

  private Q_SLOTS:
    void localModelAndResources()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("角色.model3.json"));
        write(path, R"({"Version":3,"FileReferences":{"Moc":"model.moc3","Textures":[]}})");
        write(dir.filePath(QStringLiteral("model.moc3")), "moc-data");
        ModelAssets model(QUrl::fromLocalFile(path));
        QCOMPARE(model.read(QStringLiteral("model.moc3")), QByteArray("moc-data"));
        QVERIFY_EXCEPTION_THROWN(model.read(QStringLiteral("missing.png")), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(model.read(QStringLiteral("/etc/passwd")), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(model.read(QStringLiteral("..\\outside")), std::runtime_error);
    }

    void rejectsTraversalAndSymlinkEscape()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("model")));
        const auto path = dir.filePath(QStringLiteral("model/test.model3.json"));
        write(path, R"({"Version":3,"FileReferences":{"Moc":"model.moc3","Textures":[]}})");
        const auto outside = dir.filePath(QStringLiteral("outside.json"));
        write(outside, "{}");
        QVERIFY(QFile::link(outside, dir.filePath(QStringLiteral("model/link.json"))));
        ModelAssets model(QUrl::fromLocalFile(path));
        QVERIFY_EXCEPTION_THROWN(model.read(QStringLiteral("../outside.json")), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(model.read(QStringLiteral("link.json")), std::runtime_error);
    }

    void rejectsRemoteAndInvalidModels()
    {
        QVERIFY_EXCEPTION_THROWN(ModelAssets(QUrl("https://example.com/model.model3.json")),
                                 std::runtime_error);
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("test.model3.json"));
        write(path, "{");
        QVERIFY_EXCEPTION_THROWN(ModelAssets(QUrl::fromLocalFile(path)), std::runtime_error);
        write(path, "{}");
        QVERIFY_EXCEPTION_THROWN(ModelAssets(QUrl::fromLocalFile(path)), std::runtime_error);
        write(path, R"({"Version":3,"FileReferences":{"Moc":"model.moc3","Textures":[]}})");
        write(dir.filePath(QStringLiteral("bad.json")), "[");
        write(dir.filePath(QStringLiteral("empty.moc3")), "");
        ModelAssets model(QUrl::fromLocalFile(path));
        QVERIFY_EXCEPTION_THROWN(model.readJson(QStringLiteral("bad.json")), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(model.read(QStringLiteral("empty.moc3")), std::runtime_error);
    }

    void vtubeIdleFallback()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("hiyori.model3.json"));
        const QByteArray model =
            R"({"Version":3,"FileReferences":{"Moc":"model.moc3","Textures":[],"Motions":{"TapBody":[{"File":"tap.motion3.json"}]}}})";
        write(path, model);
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("animations")));
        write(dir.filePath(QStringLiteral("animations/idle.motion3.json")), R"({"Version":3,"Curves":[]})");
        const auto config = dir.filePath(QStringLiteral("hiyori.vtube.json"));
        write(config,
              R"({"FileReferences":{"Model":"hiyori.model3.json","IdleAnimation":"idle.motion3.json"}})");
        ModelAssets assets(QUrl::fromLocalFile(path));
        const auto motions = QJsonDocument::fromJson(assets.settings())
                                 .object()["FileReferences"]
                                 .toObject()["Motions"]
                                 .toObject();
        QCOMPARE(motions["Idle"].toArray().first().toObject()["File"].toString(),
                 QStringLiteral("animations/idle.motion3.json"));
        QCOMPARE(motions["TapBody"].toArray().size(), 1);
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), model); // Import is entirely in memory.

        write(dir.filePath(QStringLiteral("idle.motion3.json")), "{}");
        ModelAssets direct(QUrl::fromLocalFile(path));
        const auto directRefs =
            QJsonDocument::fromJson(direct.settings()).object()["FileReferences"].toObject();
        QCOMPARE(directRefs["Motions"].toObject()["Idle"].toArray().first().toObject()["File"].toString(),
                 QStringLiteral("idle.motion3.json"));

        write(
            path,
            R"({"Version":3,"FileReferences":{"Moc":"model.moc3","Textures":[],"Motions":{"Idle":[{"File":"native.motion3.json"}]}}})");
        ModelAssets native(QUrl::fromLocalFile(path));
        const auto nativeRefs =
            QJsonDocument::fromJson(native.settings()).object()["FileReferences"].toObject();
        QCOMPARE(nativeRefs["Motions"].toObject()["Idle"].toArray().first().toObject()["File"].toString(),
                 QStringLiteral("native.motion3.json"));
    }

    void ignoresInvalidVtubeConfigs()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("model")));
        const auto path = dir.filePath(QStringLiteral("model/test.model3.json"));
        const QByteArray model = R"({"Version":3,"FileReferences":{"Moc":"model.moc3","Textures":[]}})";
        write(path, model);
        write(dir.filePath(QStringLiteral("outside.motion3.json")), "{}");
        write(dir.filePath(QStringLiteral("model/idle.motion3.json")), "{}");
        write(dir.filePath(QStringLiteral("model/other.model3.json")), model);
        QVERIFY(QFile::link(dir.filePath(QStringLiteral("outside.motion3.json")),
                            dir.filePath(QStringLiteral("model/link.motion3.json"))));
        const auto config = dir.filePath(QStringLiteral("model/test.vtube.json"));
        const QList<QByteArray> configs{
            "{",
            R"({"FileReferences":{"Model":"other.model3.json","IdleAnimation":"idle.motion3.json"}})",
            R"({"FileReferences":{"Model":"test.model3.json","IdleAnimation":"missing.motion3.json"}})",
            R"({"FileReferences":{"Model":"test.model3.json","IdleAnimation":"../outside.motion3.json"}})",
            R"({"FileReferences":{"Model":"test.model3.json","IdleAnimation":"link.motion3.json"}})",
            R"({"FileReferences":{"Model":"test.model3.json","IdleAnimation":"/etc/passwd"}})"};
        for (const auto &json : configs) {
            write(config, json);
            ModelAssets assets(QUrl::fromLocalFile(path));
            QCOMPARE(assets.settings(), model);
        }
    }

    void vtubeHotkeysWithoutIdle()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("test.model3.json"));
        write(
            path,
            R"({"Version":3,"FileReferences":{"Moc":"test.moc3","Textures":[],"Motions":{"Native":[{"File":"W.motion3.json"}]},"Expressions":[{"Name":"Native smile","File":"smile.exp3.json"}]}})");
        write(dir.filePath(QStringLiteral("W.motion3.json")), "{}");
        write(dir.filePath(QStringLiteral("A.motion3.json")), "{}");
        write(dir.filePath(QStringLiteral("smile.exp3.json")), "{}");
        write(dir.filePath(QStringLiteral("other.exp3.json")), "{}");
        write(dir.filePath(QStringLiteral("test.vtube.json")), R"({
            "FileReferences":{"Model":"test.model3.json","IdleAnimation":""},
            "Hotkeys":[
                {"Action":"TriggerAnimation","File":"W.motion3.json","Triggers":{"Trigger1":"W"}},
                {"Action":"TriggerAnimation","File":"../outside.motion3.json"},
                {"Action":"ToggleExpression","File":"smile.exp3.json","DeactivateAfterKeyUp":true,"Triggers":{"Trigger1":"LeftMouseButton"}},
                {"Action":"TriggerAnimation","File":"A.motion3.json","IsActive":false},
                {"Action":"TriggerAnimation","File":"A.motion3.json","FadeSecondsAmount":0.01,"Triggers":{"Trigger1":"A"}},
                {"Action":"ToggleExpression","File":"other.exp3.json","Name":"Native smile","Triggers":{"Trigger1":"N1"}}
            ]})");
        ModelAssets assets(QUrl::fromLocalFile(path));
        const auto refs = QJsonDocument::fromJson(assets.settings()).object()["FileReferences"].toObject();
        const auto motions = refs["Motions"].toObject();
        QVERIFY(!motions.contains("Idle"));
        QCOMPARE(motions.size(), 2);
        QCOMPARE(motions["A"].toArray()[0].toObject()["FadeInTime"].toDouble(), 0.01);
        QCOMPARE(refs["Expressions"].toArray().size(), 2);
        QCOMPARE(assets.hotkeys().size(), 4);
        QCOMPARE(assets.hotkeys()[0].toObject()["group"].toString(), QStringLiteral("Native"));
        QCOMPARE(assets.hotkeys()[1].toObject()["name"].toString(), QStringLiteral("Native smile"));
        QVERIFY(assets.hotkeys()[1].toObject()["onRelease"].toBool());
        QCOMPARE(assets.hotkeys()[3].toObject()["name"].toString(), QStringLiteral("Native smile (2)"));
    }

    void vtubePointerMappings()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("test.model3.json"));
        const QByteArray model = R"({"Version":3,"FileReferences":{"Moc":"model.moc3","Textures":[]}})";
        write(path, model);
        QJsonObject mapping{{"Input", "MousePositionX"},
                            {"OutputLive2D", "ParamMouseX"},
                            {"InputRangeLower", -1},
                            {"InputRangeUpper", 1},
                            {"OutputRangeLower", -30},
                            {"OutputRangeUpper", 30},
                            {"Smoothing", 20}};
        QJsonArray entries{mapping};
        auto invalid = mapping;
        invalid["Input"] = "FaceAngleX";
        entries.append(invalid);
        invalid = mapping;
        invalid["InputRangeUpper"] = -1; // Zero-width input range.
        entries.append(invalid);
        invalid = mapping;
        invalid["OutputRangeLower"] = "bad";
        entries.append(invalid);
        invalid = mapping;
        invalid.remove("OutputLive2D");
        entries.append(invalid);
        mapping["Input"] = "MousePositionY";
        mapping["OutputLive2D"] = "ParamMouseY";
        entries.append(mapping);
        const auto config = dir.filePath(QStringLiteral("test.vtube.json"));
        QJsonObject root{{"FileReferences", QJsonObject{{"Model", "test.model3.json"}}},
                         {"ParameterSettings", entries}};
        write(config, QJsonDocument(root).toJson());
        ModelAssets assets(QUrl::fromLocalFile(path));
        QCOMPARE(assets.settings(), model);
        QCOMPARE(assets.pointerMappings().size(), size_t(2));
        QCOMPARE(assets.pointerMappings()[0].parameter, QStringLiteral("ParamMouseX"));
        QCOMPARE(assets.pointerMappings()[0].target({.5, -1}), 15.0);
        QCOMPARE(assets.pointerMappings()[1].target({.5, -1}), -30.0);
        QCOMPARE(assets.pointerMappings()[1].smoothing, 20.0);
        write(dir.filePath(QStringLiteral("other.model3.json")), model);
        root["FileReferences"] = QJsonObject{{"Model", "other.model3.json"}};
        write(config, QJsonDocument(root).toJson());
        ModelAssets mismatch(QUrl::fromLocalFile(path));
        QVERIFY(mismatch.pointerMappings().empty());
    }

    void pointerRangesAndSmoothing()
    {
        PointerMapping mapping;
        mapping.inputLower = -.5;
        mapping.inputUpper = .5;
        mapping.outputLower = 30;
        mapping.outputUpper = -30; // Reversed output and extrapolation.
        QCOMPARE(mapping.target({-1, 0}), 60.0);
        QCOMPARE(mapping.target({.5, 0}), -30.0);
        mapping.clampOutput = true;
        QCOMPARE(mapping.target({-1, 0}), 30.0);
        mapping.clampOutput = false;
        mapping.clampInput = true;
        QCOMPARE(mapping.target({1, 0}), -30.0);
        std::swap(mapping.inputLower, mapping.inputUpper); // Reversed input.
        QCOMPARE(mapping.target({1, 0}), 30.0);
        mapping.smoothing = 20;
        const auto once = mapping.smooth(0, 30, .1);
        QVERIFY(once > 0 && once < 30);
        auto twice = mapping.smooth(0, 30, .05);
        twice = mapping.smooth(twice, 30, .05);
        QVERIFY(std::abs(once - twice) < 1e-12);
        QCOMPARE(mapping.smooth(once, -30, 0), once);
        mapping.smoothing = 0;
        QCOMPARE(mapping.smooth(0, -30, 0), -30.0);
    }
};

QTEST_GUILESS_MAIN(ModelAssetsTest)
#include "modelassets_test.moc"
