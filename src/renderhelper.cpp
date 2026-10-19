#include "cubismmodel.h"
#include "graphicscontext.h"
#include "renderprotocol.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
    QFile sharedFrame;
    uchar *sharedPixels = nullptr;
    bool acquired = false;
    int status = 0;
    double previousWriteMs = 0;
    while (true) {
        const auto line = input.readLine(RenderProtocol::maxHeader + 1);
        if (line.isEmpty()) {
            break;
        }
        const auto receivedNs = RenderProtocol::monotonicNs();
        QImage image;
        QJsonObject response;
#ifdef LIVE2D_VULKAN
        response.insert(QStringLiteral("backend"), QStringLiteral("vulkan"));
#else
        response.insert(QStringLiteral("backend"), QStringLiteral("opengl"));
#endif
        QElapsedTimer traceClock;
        traceClock.start();
        try {
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(line, &parseError);
            if (!line.endsWith('\n') || parseError.error || !document.isObject()) {
                throw std::runtime_error("无效的渲染请求。");
            }
            const auto request = document.object();
            if (request.value("trace").toBool()) {
                response.insert("ipc",
                                QJsonObject{{"request_wait_ms",
                                             (receivedNs - qint64(request.value("sentNs").toDouble())) / 1e6},
                                            {"previous_write_ms", previousWriteMs}});
            }
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
            const bool modelChanged = !model || source != loadedSource;
            if (modelChanged) {
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
            for (const auto &value : request.value("actions").toArray()) {
                const auto action = value.toObject();
                if (action["action"].toString() == QLatin1String("motion"))
                    model->playMotion(action["group"].toString(), action["index"].toInt(-1));
                else if (action["action"].toString() == QLatin1String("expression"))
                    model->setExpressionActive(action["name"].toString(), action["active"].toBool());
            }
            const float delta = std::clamp(request.value("delta").toDouble(), 0.0, 0.1);
            const float zoom = std::clamp(request.value("zoom").toDouble(1), 0.25, 3.0);
            const auto prepared = traceClock.nsecsElapsed();
            model->draw(
                delta, size, zoom,
                QPointF(std::clamp(request.value("gazeX").toDouble(), -1.0, 1.0),
                        std::clamp(request.value("gazeY").toDouble(), -1.0, 1.0)),
                QPointF(std::clamp(request.value("pointerX").toDouble(request.value("gazeX").toDouble()),
                                   -1.0, 1.0),
                        std::clamp(request.value("pointerY").toDouble(request.value("gazeY").toDouble()),
                                   -1.0, 1.0)),
                request.value("follow").toBool(), request.value("idle").toString());
            const auto drawn = traceClock.nsecsElapsed();
            // Resolve the output mapping before readback so the graphics
            // backend can write directly into it. Pipe clients still get an
            // owned image, including clients using an unavailable mapping.
            const auto sharedPath = request.value("sharedFrame").toString();
            uchar *outputPixels = nullptr;
            if (!sharedPath.isEmpty()) {
                if (sharedFrame.fileName() != sharedPath) {
                    sharedFrame.close();
                    sharedPixels = nullptr;
                    sharedFrame.setFileName(sharedPath);
                    if (sharedFrame.open(QIODevice::ReadWrite) &&
                        sharedFrame.size() == RenderProtocol::maxFrame)
                        sharedPixels = sharedFrame.map(0, RenderProtocol::maxFrame);
                }
                outputPixels = sharedPixels;
            }
            const auto cpuBeforeRead = RenderProtocol::threadCpuNs();
            image = context->readback(outputPixels);
            if (outputPixels) {
                response.insert("sharedFrame", true);
                response.insert("pixelBytes", int(image.sizeInBytes()));
            }
            if (request.value("trace").toBool()) {
                const auto read = traceClock.nsecsElapsed();
                const auto readCpu = RenderProtocol::threadCpuNs() - cpuBeforeRead;
                response.insert("readbackDetail", context->readbackTiming());
                response.insert("timing", QJsonObject{{"prepare_ms", prepared / 1e6},
                                                      {"draw_ms", (drawn - prepared) / 1e6},
                                                      {"readback_ms", (read - drawn) / 1e6},
                                                      {"readback_cpu_ms", readCpu / 1e6}});
            }
            // Opt-in so older receivers keep the full per-frame metadata
            // contract. New receivers retain model metadata until reload.
            if (modelChanged || !request.value("metadataOnce").toBool()) {
                response.insert("expressions", QJsonArray::fromStringList(model->expressions()));
                response.insert("motionGroups", QJsonArray::fromStringList(model->motionGroups()));
                response.insert("hotkeys", model->hotkeys());
            }
            response.insert("width", image.width());
            response.insert("height", image.height());
        } catch (const std::exception &error) {
            response.insert("error", QString::fromUtf8(error.what()));
            status = 2;
        }
        if (response.contains("ipc")) {
            auto ipc = response.value("ipc").toObject();
            ipc.insert("readyNs", double(RenderProtocol::monotonicNs()));
            response.insert("ipc", ipc);
        }
        const bool shared = response.value("sharedFrame").toBool();
        response.insert("bytes", shared ? 0 : int(image.sizeInBytes()));
        auto header = QJsonDocument(response).toJson(QJsonDocument::Compact);
        // Keep the following RGBA pixels 32-bit aligned when the receiver
        // transfers its QByteArray directly to QImage. JSON permits whitespace.
        header.append((4 - (header.size() + 1) % 4) % 4, ' ');
        header.append('\n');
        const auto writeNs = RenderProtocol::monotonicNs();
        if (output.write(header) != header.size() ||
            (!shared && !image.isNull() &&
             output.write(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()) !=
                 image.sizeInBytes()) ||
            !output.flush()) {
            status = 3;
            break;
        }
        previousWriteMs = (RenderProtocol::monotonicNs() - writeNs) / 1e6;
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
