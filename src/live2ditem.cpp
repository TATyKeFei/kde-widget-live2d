#include "live2ditem.h"
#include "desktoptracker.h"
#include "renderprotocol.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>
#include <QScreen>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>

namespace
{
class ImageContainer final : public QSGNode
{
  public:
    explicit ImageContainer(QSGImageNode *imageNode) : image(imageNode) { appendChildNode(image); }
    ~ImageContainer() override
    {
        removeChildNode(image);
        delete image;
        delete texture;
    }
    QSGImageNode *image;
    QSGTexture *texture = nullptr;
};
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

Live2DItem::Live2DItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(30000);
    connect(&m_timer, &QTimer::timeout, this, &Live2DItem::requestFrame);
    connect(&m_timeout, &QTimer::timeout, this, [this] { failRenderer(QStringLiteral("角色渲染超时。")); });
    connect(this, &QQuickItem::visibleChanged, this, &Live2DItem::refreshTimer);
    const auto watchWindow = [this](QQuickWindow *currentWindow) {
        disconnect(m_windowVisibilityConnection);
        if (currentWindow) {
            m_windowVisibilityConnection = connect(currentWindow, &QQuickWindow::visibleChanged, this,
                                                   &Live2DItem::refreshTimer, Qt::QueuedConnection);
        }
        refreshTimer();
    };
    connect(this, &QQuickItem::windowChanged, this, watchWindow);
    watchWindow(window());
}
Live2DItem::~Live2DItem()
{
    if (m_trackingSubscribed && m_tracker)
        m_tracker->unsubscribe(this);
    stopRenderer();
}

bool Live2DItem::canRender() const
{
    return isComponentComplete() && !m_source.isEmpty() && isVisible() && window() && window()->isVisible() &&
           width() > 0 && height() > 0;
}
void Live2DItem::refreshTimer()
{
    refreshTracking();
    m_timer.setInterval(std::max(1, qRound(1000.0 / m_frameRate)));
    if (canRender() && m_error.isEmpty() && m_running) {
        m_timer.start();
    } else {
        m_timer.stop();
        m_clock.invalidate();
    }
    if (canRender() && m_error.isEmpty() && (!m_ready || m_requestPending)) {
        QTimer::singleShot(0, this, &Live2DItem::requestFrame);
    }
}
void Live2DItem::refreshTracking()
{
    const bool subscribe = canRender() && m_error.isEmpty() && m_running && m_followCursor;
    if (subscribe == m_trackingSubscribed)
        return;
    if (!m_tracker && subscribe) {
        m_tracker = DesktopTracker::instance();
        connect(m_tracker, &DesktopTracker::changed, this, [this] {
            if (!m_tracker->state().valid)
                m_gaze = {};
            Q_EMIT trackingStatusChanged();
        });
    }
    m_trackingSubscribed = subscribe;
    if (subscribe)
        m_tracker->subscribe(this);
    else
        m_tracker->unsubscribe(this);
    Q_EMIT trackingStatusChanged();
}
QString Live2DItem::trackingError() const
{
    return m_trackingSubscribed && m_tracker ? m_tracker->errorString() : QString();
}
bool Live2DItem::updateDesktopGaze()
{
    if (!m_followCursor)
        return false;
    if (m_tracker && m_trackingSubscribed && m_tracker->state().valid) {
        const auto &state = m_tracker->state();
        if (!CursorTracking::follows(m_followMode, state)) {
            m_gaze = {};
            return false;
        }
        const auto *screen = window()->screen();
        const auto center = mapToGlobal(QPointF(width() / 2, height() / 2));
        m_gaze = screen ? CursorTracking::gaze(state.position, center, screen->geometry().size()) : QPointF();
        return true;
    }
    // With no compositor connection only Always can safely use local hover events.
    return m_followMode == CursorTracking::Mode::Always;
}
void Live2DItem::componentComplete()
{
    QQuickItem::componentComplete();
    refreshTimer();
}
void Live2DItem::geometryChange(const QRectF &geometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(geometry, oldGeometry);
    if (geometry.size() != oldGeometry.size()) {
        scheduleFrame();
        refreshTimer();
    }
}
void Live2DItem::setStatus(bool ready, const QString &error, const QStringList &expressions)
{
    m_ready = ready;
    m_error = error;
    m_expressions = expressions;
    refreshTimer();
    Q_EMIT statusChanged();
}
void Live2DItem::stopRenderer()
{
    m_timeout.stop();
    m_inFlight = false;
    m_requestPending = false;
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
        // Callbacks may currently be executing in a QProcess signal.
        m_process.release()->deleteLater();
    }
    m_input.clear();
    m_header = {};
    m_expectedBytes = -1;
    m_clock.invalidate();
}
void Live2DItem::failRenderer(const QString &error)
{
    const bool fallback =
        m_backend == QLatin1String("auto") && m_attempt == QLatin1String("vulkan") && !m_fellBack;
    stopRenderer();
    m_activeBackend.clear();
    m_frame = {};
    update();
    if (fallback) {
        m_fellBack = true;
        m_vulkanError = error;
        qWarning().noquote() << "Live2D Vulkan unavailable, trying OpenGL:" << error;
        setStatus(false, {});
        QTimer::singleShot(0, this, &Live2DItem::requestFrame);
    } else {
        setStatus(false,
                  m_fellBack ? QStringLiteral("Vulkan：%1\nOpenGL：%2").arg(m_vulkanError, error) : error);
    }
}
void Live2DItem::scheduleFrame()
{
    m_requestPending = true;
    QTimer::singleShot(0, this, &Live2DItem::requestFrame);
}
void Live2DItem::requestFrame()
{
    if (!canRender() || m_inFlight || !m_error.isEmpty()) {
        return;
    }
    if (!m_process) {
        m_attempt = m_backend == QLatin1String("auto")
                        ? (m_fellBack ? QStringLiteral("opengl") : QStringLiteral("vulkan"))
                        : m_backend;
        const auto path = helperPath(m_attempt);
        if (!QFileInfo(path).isExecutable()) {
            failRenderer(QStringLiteral("找不到渲染程序：%1").arg(path));
            return;
        }
        m_process = std::make_unique<QProcess>();
        connect(m_process.get(), &QProcess::started, this, &Live2DItem::requestFrame);
        connect(m_process.get(), &QProcess::readyReadStandardOutput, this, &Live2DItem::readFrames);
        connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart && m_process) {
                failRenderer(m_process->errorString());
            }
        });
        connect(m_process.get(), &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
            if (m_process) {
                failRenderer(QStringLiteral("角色渲染进程已退出（%1）。").arg(code));
            }
        });
        m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_timeout.start();
        m_process->start(path, {});
        return;
    }
    if (m_process->state() != QProcess::Running) {
        return;
    }
    m_requestPending = false;
    // Bound readback cost on high-DPI desktops, keeping the original aspect ratio.
    const auto pixels = [this](qreal dimension) {
        return qMax(1, qRound(std::clamp(dimension * window()->effectiveDevicePixelRatio(), 1.0, 32768.0)));
    };
    QSize size(pixels(width()), pixels(height()));
    if (size.width() > RenderProtocol::maxDimension || size.height() > RenderProtocol::maxDimension) {
        size.scale(RenderProtocol::maxDimension, RenderProtocol::maxDimension, Qt::KeepAspectRatio);
        size.setWidth(qMax(1, size.width()));
        size.setHeight(qMax(1, size.height()));
    }
    const double delta =
        m_running && m_clock.isValid() ? std::clamp(m_clock.elapsed() / 1000.0, 0.0, 0.1) : 0;
    m_clock.start();
    QJsonArray taps;
    for (const auto &tap : std::as_const(m_taps)) {
        taps.append(QJsonArray{tap.x(), tap.y()});
    }
    m_taps.clear();
    const bool follow = updateDesktopGaze();
    QJsonObject request{{"source", m_source.toString()},
                        {"width", size.width()},
                        {"height", size.height()},
                        {"delta", delta},
                        {"zoom", m_zoom},
                        {"follow", follow},
                        {"gazeX", m_gaze.x()},
                        {"gazeY", m_gaze.y()},
                        {"idle", m_idleGroup},
                        {"taps", taps},
                        {"expression", m_expression}};
    m_expression.clear();
    const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
    if (bytes.size() > RenderProtocol::maxHeader) {
        failRenderer(QStringLiteral("角色渲染请求超过限制。"));
        return;
    }
    if (m_process->write(bytes) != bytes.size()) {
        failRenderer(QStringLiteral("无法发送角色渲染请求。"));
        return;
    }
    m_inFlight = true;
    m_timeout.start();
}
void Live2DItem::readFrames()
{
    if (!m_process) {
        return;
    }
    m_input += m_process->readAllStandardOutput();
    if (m_input.size() > RenderProtocol::maxFrame + RenderProtocol::maxHeader) {
        failRenderer(QStringLiteral("角色渲染响应超过限制。"));
        return;
    }
    if (m_expectedBytes < 0) {
        const auto end = m_input.indexOf('\n');
        if (end < 0) {
            if (m_input.size() > RenderProtocol::maxHeader) {
                failRenderer(QStringLiteral("无效的渲染响应头。"));
            }
            return;
        }
        if (end > RenderProtocol::maxHeader) {
            failRenderer(QStringLiteral("渲染响应头超过限制。"));
            return;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(m_input.left(end), &parseError);
        m_input.remove(0, end + 1);
        if (parseError.error || !document.isObject()) {
            failRenderer(QStringLiteral("无法解析角色渲染响应。"));
            return;
        }
        m_header = document.object();
        if (m_header.contains("error")) {
            failRenderer(m_header.value("error").toString());
            return;
        }
        const QSize size(m_header.value("width").toInt(), m_header.value("height").toInt());
        m_expectedBytes = m_header.value("bytes").toInt(-1);
        if (!m_inFlight || !RenderProtocol::validSize(size) ||
            m_expectedBytes != size.width() * size.height() * 4 ||
            m_header.value("backend").toString() != m_attempt) {
            failRenderer(QStringLiteral("无效的角色渲染帧。"));
            return;
        }
    }
    if (m_input.size() < m_expectedBytes) {
        return;
    }
    const QSize size(m_header.value("width").toInt(), m_header.value("height").toInt());
    m_frame = QImage(reinterpret_cast<const uchar *>(m_input.constData()), size.width(), size.height(),
                     QImage::Format_RGBA8888_Premultiplied)
                  .copy();
    m_input.remove(0, m_expectedBytes);
    if (!m_input.isEmpty()) {
        failRenderer(QStringLiteral("角色渲染响应包含多余数据。"));
        return;
    }
    m_expectedBytes = -1;
    m_frameDirty = true;
    m_inFlight = false;
    m_timeout.stop();
    m_activeBackend = m_attempt;
    QStringList expressions;
    for (const auto &name : m_header.value("expressions").toArray()) {
        expressions.append(name.toString());
    }
    if (!m_ready || expressions != m_expressions) {
        setStatus(true, {}, expressions);
    }
    update();
    if (m_requestPending) {
        QTimer::singleShot(0, this, &Live2DItem::requestFrame);
    }
}
QSGNode *Live2DItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    auto *node = static_cast<ImageContainer *>(oldNode);
    if (m_frame.isNull()) {
        delete node;
        return nullptr;
    }
    if (!node) {
        auto *imageNode = window()->createImageNode();
        if (!imageNode) {
            return nullptr;
        }
        imageNode->setOwnsTexture(false);
        node = new ImageContainer(imageNode);
        m_frameDirty = true;
    }
    if (m_frameDirty) {
        auto *texture = window()->createTextureFromImage(m_frame);
        if (!texture) {
            delete node;
            return nullptr;
        }
        node->image->setTexture(texture);
        delete node->texture;
        node->texture = texture;
        m_frameDirty = false;
    }
    node->image->setRect(boundingRect());
    node->image->setFiltering(QSGTexture::Linear);
    return node;
}

void Live2DItem::setSource(const QUrl &source)
{
    if (m_source == source) {
        return;
    }
    m_source = source;
    reload();
    Q_EMIT sourceChanged();
}
void Live2DItem::reload()
{
    stopRenderer();
    m_fellBack = false;
    m_activeBackend.clear();
    m_vulkanError.clear();
    m_taps.clear();
    m_expression.clear();
    m_frame = {};
    m_frameDirty = true;
    setStatus(false, {});
    update();
}
void Live2DItem::setRenderBackend(const QString &backend)
{
    if (backend != QLatin1String("auto") && backend != QLatin1String("vulkan") &&
        backend != QLatin1String("opengl")) {
        return;
    }
    if (m_backend == backend) {
        return;
    }
    m_backend = backend;
    reload();
    Q_EMIT renderBackendChanged();
}
void Live2DItem::setRunning(bool running)
{
    if (m_running == running) {
        return;
    }
    m_running = running;
    m_clock.invalidate();
    refreshTimer();
    Q_EMIT runningChanged();
}
void Live2DItem::setFrameRate(int rate)
{
    rate = std::clamp(rate, 1, 60);
    if (rate == m_frameRate) {
        return;
    }
    m_frameRate = rate;
    refreshTimer();
    Q_EMIT frameRateChanged();
}
void Live2DItem::setZoom(qreal zoom)
{
    if (!std::isfinite(zoom)) {
        return;
    }
    zoom = std::clamp(zoom, 0.25, 3.0);
    if (qFuzzyCompare(zoom, m_zoom)) {
        return;
    }
    m_zoom = zoom;
    scheduleFrame();
    Q_EMIT zoomChanged();
}
void Live2DItem::setFollowCursor(bool follow)
{
    setCursorFollowMode(follow ? QStringLiteral("always") : QStringLiteral("off"));
}
void Live2DItem::setCursorFollowMode(const QString &mode)
{
    CursorTracking::Mode parsed;
    if (mode == m_cursorFollowMode || !CursorTracking::parseMode(mode, parsed))
        return;
    m_cursorFollowMode = mode;
    m_followMode = parsed;
    const bool follow = parsed != CursorTracking::Mode::Off;
    const bool changed = follow != m_followCursor;
    m_followCursor = follow;
    m_gaze = {};
    refreshTracking();
    scheduleFrame();
    if (changed)
        Q_EMIT followCursorChanged();
    Q_EMIT cursorFollowModeChanged();
}
void Live2DItem::setIdleGroup(const QString &group)
{
    if (m_idleGroup == group) {
        return;
    }
    m_idleGroup = group;
    Q_EMIT idleGroupChanged();
}
void Live2DItem::setGaze(qreal x, qreal y)
{
    if (!m_followCursor || (m_tracker && m_trackingSubscribed && m_tracker->state().valid) ||
        !std::isfinite(x) || !std::isfinite(y)) {
        return;
    }
    m_gaze = QPointF(std::clamp(x, -1.0, 1.0), std::clamp(y, -1.0, 1.0));
}
void Live2DItem::tap(qreal x, qreal y)
{
    if (!m_ready || !m_running || !std::isfinite(x) || !std::isfinite(y)) {
        return;
    }
    if (m_taps.size() < 8) {
        m_taps.append(QPointF(std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0)));
        scheduleFrame();
    }
}
void Live2DItem::setExpression(const QString &name)
{
    if (!m_ready || !m_running || !m_expressions.contains(name)) {
        return;
    }
    m_expression = name;
    scheduleFrame();
}
