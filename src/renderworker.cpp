#include "renderworker.h"
#include "renderprotocol.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QThread>
#include <cstring>
#include <dlfcn.h>

namespace
{
QString helperPath(const QString &backend)
{
    const auto executable = QStringLiteral("live2d-render-") + backend;
    static int moduleMarker;
    Dl_info location{};
    if (dladdr(&moduleMarker, &location) && location.dli_fname) {
        const QDir directory(QFileInfo(QString::fromLocal8Bit(location.dli_fname)).absolutePath());
        const QDir build(QStringLiteral(LIVE2D_HELPER_BUILD_DIR));
        if (directory.canonicalPath() == build.canonicalPath()) {
            return build.filePath(executable);
        }
        return QDir(directory.filePath(QStringLiteral(LIVE2D_HELPER_RELATIVE))).filePath(executable);
    }
    return {};
}
} // namespace

RenderWorker::~RenderWorker() { clearProcess(); }

void RenderWorker::clearProcess()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_process) {
        m_process->disconnect(this);
        // Reap the killed child on this worker, never in a GUI-thread
        // destructor or reload callback. The GUI does not join this thread.
        m_process->kill();
        m_process->waitForFinished(1000);
        m_process.reset();
    }
    m_sharedPixels = nullptr;
    m_sharedFrame.reset();
    m_input.clear();
    m_header = {};
    m_expectedBytes = -1;
    m_inFlight = false;
}

void RenderWorker::stop(quint64 generation)
{
    clearProcess();
    m_generation = generation;
}

void RenderWorker::start(quint64 generation, const QString &backend)
{
    stop(generation);
    m_backend = backend;
    const auto path = helperPath(backend);
    if (!QFileInfo(path).isExecutable()) {
        fail(QStringLiteral("找不到渲染程序：%1").arg(path));
        return;
    }
    m_sharedFrame = std::make_unique<QTemporaryFile>();
    if (m_sharedFrame->open() && m_sharedFrame->resize(RenderProtocol::maxFrame))
        m_sharedPixels = m_sharedFrame->map(0, RenderProtocol::maxFrame);
    if (!m_sharedPixels)
        m_sharedFrame.reset();
    m_process = std::make_unique<QProcess>();
    connect(m_process.get(), &QProcess::started, this, [this] { Q_EMIT started(m_generation); });
    connect(m_process.get(), &QProcess::readyReadStandardOutput, this, &RenderWorker::readFrames);
    connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(m_process->errorString());
    });
    connect(m_process.get(), &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        fail(QStringLiteral("角色渲染进程已退出（%1）。").arg(code));
    });
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_process->start(path, {});
}

void RenderWorker::fail(const QString &error)
{
    // Do not destroy QProcess from inside its own signal. The GUI posts stop
    // after accepting this error; disconnect to suppress duplicate failures.
    if (m_process)
        m_process->disconnect(this);
    Q_EMIT failed(m_generation, error);
}

void RenderWorker::request(quint64 generation, QJsonObject request)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (generation != m_generation)
        return;
    if (!m_process || m_process->state() != QProcess::Running || m_inFlight) {
        fail(QStringLiteral("角色渲染请求状态无效。"));
        return;
    }
    if (m_sharedPixels)
        request.insert("sharedFrame", m_sharedFrame->fileName());
    request.insert("metadataOnce", true);
    const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > RenderProtocol::maxHeader) {
        fail(QStringLiteral("角色渲染请求超过限制。"));
        return;
    }
    m_trace = request.value("trace").toBool();
    m_firstByte = 0;
    m_readCount = 0;
    m_inFlight = true;
    if (m_process->write(bytes) != bytes.size())
        fail(QStringLiteral("无法发送角色渲染请求。"));
}

void RenderWorker::readFrames()
{
    if (m_trace) {
        if (!m_firstByte)
            m_firstByte = RenderProtocol::monotonicNs();
        ++m_readCount;
    }
    m_input += m_process->readAllStandardOutput();
    if (m_input.size() > RenderProtocol::maxFrame + RenderProtocol::maxHeader) {
        fail(QStringLiteral("角色渲染响应超过限制。"));
        return;
    }
    if (m_expectedBytes < 0) {
        const auto end = m_input.indexOf('\n');
        if (end < 0) {
            if (m_input.size() > RenderProtocol::maxHeader)
                fail(QStringLiteral("无效的渲染响应头。"));
            return;
        }
        if (end > RenderProtocol::maxHeader) {
            fail(QStringLiteral("渲染响应头超过限制。"));
            return;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(m_input.left(end), &parseError);
        m_input.remove(0, end + 1);
        if (parseError.error || !document.isObject()) {
            fail(QStringLiteral("无法解析角色渲染响应。"));
            return;
        }
        m_header = document.object();
        if (m_header.contains("error")) {
            fail(m_header.value("error").toString());
            return;
        }
        const QSize size(m_header.value("width").toInt(), m_header.value("height").toInt());
        m_expectedBytes = m_header.value("bytes").toInt(-1);
        const bool shared = m_header.value("sharedFrame").toBool();
        if (!m_inFlight || !RenderProtocol::validSize(size) ||
            (shared ? (!m_sharedPixels || m_expectedBytes != 0 ||
                       m_header.value("pixelBytes").toInt() != size.width() * size.height() * 4)
                    : m_expectedBytes != size.width() * size.height() * 4) ||
            m_header.value("backend").toString() != m_backend) {
            fail(QStringLiteral("无效的角色渲染帧。"));
            return;
        }
    }
    if (m_input.size() < m_expectedBytes)
        return;
    if (m_input.size() != m_expectedBytes) {
        fail(QStringLiteral("角色渲染响应包含多余数据。"));
        return;
    }
    const auto received = RenderProtocol::monotonicNs();
    m_header.insert("receivedNs", double(received));
    const QSize size(m_header.value("width").toInt(), m_header.value("height").toInt());
    QImage frame;
    if (m_header.value("sharedFrame").toBool()) {
        // A queued signal must never reference memory the next helper request
        // can overwrite. Allocate and copy here, off the GUI thread.
        frame = QImage(m_sharedPixels, size.width(), size.height(), size.width() * 4,
                       QImage::Format_RGBA8888_Premultiplied)
                    .copy();
        m_input.clear();
    } else {
        auto pixels = std::make_unique<QByteArray>(std::move(m_input));
        if (reinterpret_cast<quintptr>(pixels->constData()) % alignof(quint32)) {
            frame = QImage(size, QImage::Format_RGBA8888_Premultiplied);
            if (!frame.isNull())
                std::memcpy(frame.bits(), pixels->constData(), size_t(pixels->size()));
        } else {
            frame = QImage(
                reinterpret_cast<const uchar *>(pixels->constData()), size.width(), size.height(),
                size.width() * 4, QImage::Format_RGBA8888_Premultiplied,
                [](void *data) { delete static_cast<QByteArray *>(data); }, pixels.get());
            pixels.release();
        }
    }
    if (frame.isNull()) {
        fail(QStringLiteral("无法分配角色图像内存。"));
        return;
    }
    if (m_trace) {
        auto ipc = m_header.value("ipc").toObject();
        const auto ready = qint64(ipc.take("readyNs").toDouble());
        if (ready) {
            ipc.insert("first_byte_ms", (m_firstByte - ready) / 1e6);
            ipc.insert("receive_ms", (received - m_firstByte) / 1e6);
            ipc.insert("read_events", m_readCount);
        }
        ipc.insert("image_copy_ms", (RenderProtocol::monotonicNs() - received) / 1e6);
        m_header.insert("ipc", ipc);
    }
    m_expectedBytes = -1;
    m_inFlight = false;
    Q_EMIT frameReady(m_generation, frame, m_header);
    m_header = {};
}
