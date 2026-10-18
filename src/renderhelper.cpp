#include "cubismmodel.h"
#include "graphicscontext.h"
#include "renderprotocol.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unistd.h>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Core prints its version directly to stdout. Reserve a duplicate pipe for
    // the binary protocol, and route all third-party stdout logging to stderr.
    std::fflush(stdout);
    const int protocolOutput = dup(STDOUT_FILENO);
    if (protocolOutput < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
        return 1;
    }
    QFile input, output;
    if (!input.open(stdin, QIODevice::ReadOnly) ||
        !output.open(protocolOutput, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
        return 1;
    }
    std::unique_ptr<GraphicsContext> context;
    std::unique_ptr<CubismModel> model;
    QUrl loadedSource;
    bool acquired = false;
    int status = 0;
    while (true) {
        const auto line = input.readLine(RenderProtocol::maxHeader + 1);
        if (line.isEmpty()) {
            break;
        }
        QImage image;
        QJsonObject response;
#ifdef LIVE2D_VULKAN
        response.insert(QStringLiteral("backend"), QStringLiteral("vulkan"));
#else
        response.insert(QStringLiteral("backend"), QStringLiteral("opengl"));
#endif
        try {
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(line, &parseError);
            if (!line.endsWith('\n') || parseError.error || !document.isObject()) {
                throw std::runtime_error("无效的渲染请求。");
            }
            const auto request = document.object();
            const QSize size(request.value("width").toInt(), request.value("height").toInt());
            if (!RenderProtocol::validSize(size)) {
                throw std::runtime_error("渲染尺寸超过限制。");
            }
            if (!context) {
                context = createGraphicsContext();
            }
            context->resize(size);
            context->begin();
            if (!acquired) {
                acquireCubism(context.get(), true);
                acquired = true;
            }
            const QUrl source(request.value("source").toString());
            if (!model || source != loadedSource) {
                model.reset();
                model = std::make_unique<CubismModel>(source, size);
                loadedSource = source;
            }
            const auto taps = request.value("taps").toArray();
            for (const auto &tap : taps) {
                const auto point = tap.toArray();
                if (point.size() == 2) {
                    model->tap(QPointF(point[0].toDouble(), point[1].toDouble()));
                }
            }
            model->setExpression(request.value("expression").toString());
            const float delta = std::clamp(request.value("delta").toDouble(), 0.0, 0.1);
            const float zoom = std::clamp(request.value("zoom").toDouble(1), 0.25, 3.0);
            model->draw(delta, size, zoom,
                        QPointF(std::clamp(request.value("gazeX").toDouble(), -1.0, 1.0),
                                std::clamp(request.value("gazeY").toDouble(), -1.0, 1.0)),
                        request.value("follow").toBool(), request.value("idle").toString());
            image = context->readback();
            response.insert("expressions", QJsonArray::fromStringList(model->expressions()));
            response.insert("width", image.width());
            response.insert("height", image.height());
        } catch (const std::exception &error) {
            response.insert("error", QString::fromUtf8(error.what()));
            status = 2;
        }
        response.insert("bytes", int(image.sizeInBytes()));
        const auto header = QJsonDocument(response).toJson(QJsonDocument::Compact) + '\n';
        if (output.write(header) != header.size() ||
            (!image.isNull() && output.write(reinterpret_cast<const char *>(image.constBits()),
                                             image.sizeInBytes()) != image.sizeInBytes()) ||
            !output.flush()) {
            status = 3;
            break;
        }
        if (status) {
            break;
        }
    }
    model.reset();
    if (acquired) {
        releaseCubism();
    }
    context.reset();
    return status;
}
