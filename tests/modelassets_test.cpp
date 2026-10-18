#include "modelassets.h"
#include <QFile>
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
};

QTEST_GUILESS_MAIN(ModelAssetsTest)
#include "modelassets_test.moc"
