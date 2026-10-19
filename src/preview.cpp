#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("live2d-preview"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setWindowIcon(QIcon(QStringLiteral(":/live2d/logo.png")));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Live2D 原生渲染预览"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("model"), QStringLiteral("本地 .model3.json 路径"),
                                 QStringLiteral("[model]"));
    parser.addOption({QStringLiteral("quit-after"), QStringLiteral("在指定毫秒数后退出（用于冒烟检查）"),
                      QStringLiteral("ms")});
    parser.addOption({QStringLiteral("snapshot"), QStringLiteral("退出前保存窗口为 PNG（配合 --quit-after）"),
                      QStringLiteral("path")});
    parser.addOption({QStringLiteral("render-backend"), QStringLiteral("角色渲染后端：auto、vulkan、opengl"),
                      QStringLiteral("backend"), QStringLiteral("auto")});
    parser.process(app);
    const auto backend = parser.value(QStringLiteral("render-backend"));
    if (backend != "auto" && backend != "vulkan" && backend != "opengl") {
        qCritical("Invalid --render-backend");
        return 2;
    }

    QQmlApplicationEngine engine;
    const QDir buildDirectory(QStringLiteral(LIVE2D_QML_BUILD_DIR) + QStringLiteral("/.."));
    if (QDir(QCoreApplication::applicationDirPath()).canonicalPath() == buildDirectory.canonicalPath()) {
        engine.addImportPath(QStringLiteral(LIVE2D_QML_BUILD_DIR));
    }
    engine.rootContext()->setContextProperty(QStringLiteral("initialBackend"), backend);
    const auto args = parser.positionalArguments();
    engine.rootContext()->setContextProperty(
        QStringLiteral("initialModel"),
        args.isEmpty() ? QUrl() : QUrl::fromLocalFile(QDir::current().absoluteFilePath(args.first())));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.load(QUrl(QStringLiteral("qrc:/preview/Main.qml")));
    if (parser.isSet(QStringLiteral("quit-after"))) {
        bool ok = false;
        const int ms = parser.value(QStringLiteral("quit-after")).toInt(&ok);
        if (!ok || ms < 1) {
            qCritical("--quit-after must be a positive number of milliseconds");
            return 2;
        }
        QTimer::singleShot(ms, &app, [&] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
            auto *model = window ? window->findChild<QObject *>(QStringLiteral("live2dModel")) : nullptr;
            int status = model && model->property("ready").toBool() ? 0 : 2;
            if (parser.isSet(QStringLiteral("snapshot"))) {
                if (!window || !window->grabWindow().save(parser.value(QStringLiteral("snapshot")))) {
                    status = 3;
                }
            }
            if (model) {
                qInfo().noquote() << "Live2D ready:" << model->property("ready").toBool()
                                  << model->property("activeBackend").toString()
                                  << model->property("errorString").toString();
            }
            app.exit(status);
        });
    }
    return app.exec();
}
