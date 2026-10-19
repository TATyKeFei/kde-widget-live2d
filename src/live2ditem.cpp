#include "live2ditem.h"
#include "desktoptracker.h"
#include "globalinput.h"
#include "inputkeys.h"
#include "renderprotocol.h"
#include "renderworker.h"
#include <QThread>
#ifdef LIVE2D_RHI_TEXTURE
#include "frametexture.h"
#include <QSGRendererInterface>
#endif
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>
#include <QScreen>
#include <algorithm>
#include <cmath>

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
} // namespace

Live2DItem::Live2DItem(QQuickItem *parent) : QQuickItem(parent)
{
    auto *thread = new QThread;
    thread->setObjectName(QStringLiteral("live2d-transport"));
    m_worker = new RenderWorker;
    m_worker->moveToThread(thread);
    connect(m_worker, &QObject::destroyed, thread, &QThread::quit, Qt::DirectConnection);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(m_worker, &RenderWorker::started, this, [this](quint64 generation) {
        if (generation != m_generation)
            return;
        m_rendererStarted = true;
        requestFrame();
    });
    connect(m_worker, &RenderWorker::failed, this, [this](quint64 generation, const QString &error) {
        if (generation == m_generation)
            failRenderer(error);
    });
    connect(m_worker, &RenderWorker::frameReady, this,
            [this](quint64 generation, const QImage &frame, const QJsonObject &header) {
                if (generation != m_generation)
                    return;
                m_pendingFrame = frame;
                m_header = header;
                m_responseReady = true;
                if (m_trace)
                    m_traceReceived = m_scheduleClock.nsecsElapsed();
                m_timeout.stop();
                readFrames();
            });
    thread->start();
    m_relativeClock.start();
    m_scheduleClock.start();
    m_trace = qEnvironmentVariableIntValue("LIVE2D_TRACE_SECONDS") > 0;
    if (m_trace) {
        auto *heartbeat = new QTimer(this);
        heartbeat->setInterval(100);
        connect(heartbeat, &QTimer::timeout, this, [this, heartbeat] {
            const auto now = m_scheduleClock.nsecsElapsed();
            if (m_traceHeartbeat)
                m_traceGuiLag = std::max(m_traceGuiLag, now - m_traceHeartbeat - 100000000);
            m_traceHeartbeat = now;
            if (now > qint64(std::clamp(qEnvironmentVariableIntValue("LIVE2D_TRACE_SECONDS"), 1, 300)) *
                          1000000000) {
                m_trace = false;
                heartbeat->stop();
                qWarning() << "Live2D trace finished" << this;
            }
        });
        heartbeat->start();
        qWarning() << "Live2D trace started" << this;
        if (qEnvironmentVariableIntValue("LIVE2D_TRACE_BACKEND_COMPARE") == 1) {
            QTimer::singleShot(30000, this, [this] {
                const auto previousBackend = renderBackend();
                const auto comparisonBackend = activeBackend() == QLatin1String("opengl")
                                                   ? QStringLiteral("vulkan")
                                                   : QStringLiteral("opengl");
                qWarning() << "Live2D trace backend comparison:" << comparisonBackend << this;
                setRenderBackend(comparisonBackend);
                QTimer::singleShot(30000, this, [this, previousBackend, comparisonBackend] {
                    if (renderBackend() == comparisonBackend)
                        setRenderBackend(previousBackend);
                    qWarning() << "Live2D trace backend comparison: restored" << this;
                });
            });
        }
        if (qEnvironmentVariableIntValue("LIVE2D_TRACE_POINTER_COMPARE") == 1) {
            QTimer::singleShot(30000, this, [this] {
                const auto previousMode = cursorFollowMode();
                qWarning() << "Live2D trace pointer comparison: follow off" << this;
                setCursorFollowMode(QStringLiteral("off"));
                QTimer::singleShot(30000, this, [this, previousMode] {
                    // Respect a user change made during the comparison period.
                    if (cursorFollowMode() == QLatin1String("off"))
                        setCursorFollowMode(previousMode);
                    qWarning() << "Live2D trace pointer comparison: restored" << this;
                });
            });
        }
    }
    setFlag(ItemHasContents);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setSingleShot(true);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(30000);
    connect(&m_timer, &QTimer::timeout, this, &Live2DItem::requestFrame);
    connect(&m_timeout, &QTimer::timeout, this, [this] { failRenderer(QStringLiteral("角色渲染超时。")); });
    connect(this, &QQuickItem::visibleChanged, this, &Live2DItem::refreshTimer);
    connect(this, &QQuickItem::activeFocusChanged, this, [this] {
        if (!hasActiveFocus() && m_inputMode == QLatin1String("focused"))
            releaseInput();
    });
    const auto watchWindow = [this](QQuickWindow *currentWindow) {
        disconnect(m_windowVisibilityConnection);
        disconnect(m_windowActiveConnection);
        if (currentWindow) {
            m_windowVisibilityConnection = connect(currentWindow, &QQuickWindow::visibleChanged, this,
                                                   &Live2DItem::refreshTimer, Qt::QueuedConnection);
            m_windowActiveConnection = connect(currentWindow, &QQuickWindow::activeChanged, this, [this] {
                if (window() && !window()->isActive() && m_inputMode == QLatin1String("focused"))
                    releaseInput();
            });
        }
        refreshTimer();
    };
    connect(this, &QQuickItem::windowChanged, this, watchWindow);
    watchWindow(window());
}
Live2DItem::~Live2DItem()
{
    m_running = false; // stopRenderer must not resubscribe relative motion during destruction.
    if (m_inputSubscribed && m_globalInput)
        m_globalInput->unsubscribe(this);
    if (m_trackingSubscribed && m_tracker)
        m_tracker->unsubscribe(this);
    stopRenderer();
    // The worker owns all process and mapping cleanup. Never join it from the
    // Plasma GUI thread, including while a frame copy or driver exit is slow.
    QMetaObject::invokeMethod(m_worker, &QObject::deleteLater, Qt::QueuedConnection);
}

bool Live2DItem::canRender() const
{
    return isComponentComplete() && !m_source.isEmpty() && isVisible() && window() && window()->isVisible() &&
           width() > 0 && height() > 0;
}
void Live2DItem::refreshTimer()
{
    refreshTracking();
    refreshInput();
    if (!canRender() || !m_error.isEmpty() || !m_running) {
        m_clock.invalidate();
        m_pacer.reset();
    }
    queueFrame();
}
void Live2DItem::queueFrame()
{
    m_timer.stop();
    if (m_responseReady && !m_frameDirty) {
        readFrames();
        return;
    }
    if (!canRender() || m_inFlight || !m_error.isEmpty() || (!m_running && m_ready && !m_requestPending)) {
        return;
    }
    // Timer rounding/late wakeups must not move every subsequent deadline.
    // Keep the phase on a monotonic timeline, while retaining backpressure.
    const qint64 remaining = !m_requestPending && m_ready && m_running
                                 ? m_pacer.remaining(m_scheduleClock.nsecsElapsed(), m_frameRate)
                                 : 0;
    if (m_trace)
        m_traceTimerDue = m_scheduleClock.nsecsElapsed() + remaining;
    m_timer.start(int((remaining + 999999) / 1000000));
}
void Live2DItem::refreshTracking()
{
    const bool needsCompositor =
        m_mouseTrackingMode == QLatin1String("screen") || m_followMode != CursorTracking::Mode::Always;
    const bool subscribe = canRender() && m_error.isEmpty() && m_running && m_followCursor && needsCompositor;
    if (subscribe == m_trackingSubscribed)
        return;
    if (!m_tracker && subscribe) {
        m_tracker = DesktopTracker::instance();
        connect(m_tracker, &DesktopTracker::changed, this, [this] {
            if (m_mouseTrackingMode == QLatin1String("relative") && !relativeFollowAllowed())
                resetRelativePointer();
            else if (!m_tracker->state().valid && m_mouseTrackingMode == QLatin1String("screen"))
                m_gaze = m_pointer = {};
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
    if (m_mouseTrackingMode == QLatin1String("relative")) {
        QStringList errors;
        if (m_trackingSubscribed && m_tracker && m_followMode != CursorTracking::Mode::Always &&
            !m_tracker->errorString().isEmpty())
            errors.append(m_tracker->errorString());
        if (m_relativeSubscribed && m_globalInput && !m_globalInput->motionErrorString().isEmpty())
            errors.append(m_globalInput->motionErrorString());
        return errors.join(QLatin1Char('\n'));
    }
    return m_trackingSubscribed && m_tracker ? m_tracker->errorString() : QString();
}
bool Live2DItem::relativeFollowAllowed() const
{
    return m_followCursor &&
           (m_followMode == CursorTracking::Mode::Always ||
            (m_tracker && m_trackingSubscribed && CursorTracking::follows(m_followMode, m_tracker->state())));
}
void Live2DItem::resetRelativePointer()
{
    m_relativePointer.reset();
    m_gaze = m_pointer = {};
}
bool Live2DItem::updateDesktopGaze()
{
    if (!m_followCursor)
        return false;
    if (m_mouseTrackingMode == QLatin1String("relative")) {
        if (!relativeFollowAllowed()) {
            resetRelativePointer();
            return false;
        }
        m_gaze = m_pointer = m_relativePointer.position(m_relativeClock.nsecsElapsed() / 1e9);
        return true;
    }
    if (m_tracker && m_trackingSubscribed && m_tracker->state().valid) {
        const auto &state = m_tracker->state();
        if (!CursorTracking::follows(m_followMode, state)) {
            m_gaze = m_pointer = {};
            return false;
        }
        const auto *screen = window()->screen();
        const auto center = mapToGlobal(QPointF(width() / 2, height() / 2));
        m_gaze = screen ? CursorTracking::gaze(state.position, center, screen->geometry().size()) : QPointF();
        m_pointer =
            screen ? CursorTracking::screenPosition(state.position, QRectF(screen->geometry())) : QPointF();
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
    m_modelInput.clear();
    m_modelInput.setBindings({});
    m_hotkeys = {};
    m_actions = {};
    m_motionGroups.clear();
    resetRelativePointer();
    refreshInput();
    m_timeout.stop();
    m_timer.stop();
    m_inFlight = false;
    m_requestPending = false;
    const auto generation = ++m_generation;
    auto *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, generation] { worker->stop(generation); }, Qt::QueuedConnection);
    m_rendererStarting = m_rendererStarted = false;
    m_pendingFrame = {};
    m_header = {};
    m_displayHeader = {};
    m_responseReady = false;
    m_clock.invalidate();
    m_pacer.reset();
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
    queueFrame();
}
void Live2DItem::requestFrame()
{
    // One displayed/pending image plus at most one helper response. The helper
    // may prepare the next frame while Qt is synchronizing the current one.
    if (!canRender() || m_inFlight || !m_error.isEmpty() || (!m_running && m_ready && !m_requestPending)) {
        return;
    }
    if (!m_rendererStarting) {
        m_attempt = m_backend == QLatin1String("auto")
                        ? (m_fellBack ? QStringLiteral("opengl") : QStringLiteral("vulkan"))
                        : m_backend;
        m_rendererStarting = true;
        const auto generation = m_generation;
        const auto backend = m_attempt;
        auto *worker = m_worker;
        QMetaObject::invokeMethod(
            worker, [worker, generation, backend] { worker->start(generation, backend); },
            Qt::QueuedConnection);
        m_timeout.start();
        return;
    }
    if (!m_rendererStarted)
        return;
    if (m_trace) {
        m_traceRequest = m_scheduleClock.nsecsElapsed();
        m_traceTimerLate = m_traceTimerDue ? std::max(qint64(0), m_traceRequest - m_traceTimerDue) : 0;
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
        m_running && m_clock.isValid() ? std::clamp(m_clock.nsecsElapsed() / 1.0e9, 0.0, 0.1) : 0;
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
                        {"pointerX", m_pointer.x()},
                        {"pointerY", m_pointer.y()},
                        {"idle", m_idleGroup},
                        {"taps", taps},
                        {"expression", m_expression}};
    if (m_trace) {
        request.insert("trace", true);
        request.insert("sentNs", double(RenderProtocol::monotonicNs()));
    }
    request.insert("actions", m_actions);
    m_actions = {};
    m_expression.clear();
    const auto generation = m_generation;
    auto *worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, generation, request] { worker->request(generation, request); },
        Qt::QueuedConnection);
    m_inFlight = true;
    if (m_running)
        m_pacer.requested(m_scheduleClock.nsecsElapsed(), m_frameRate);
    m_timeout.start();
}
void Live2DItem::readFrames()
{
    // Only implicitly shared image handles cross to the GUI thread. Parsing,
    // allocation and pixel copying have already finished on the worker.
    if (!m_responseReady || (m_frameDirty && !m_frame.isNull()))
        return;
    m_frame = std::move(m_pendingFrame);
    m_displayHeader = m_header;
    m_displayRequest = m_traceRequest;
    m_displayReceived = m_traceReceived;
    m_displayTimerLate = m_traceTimerLate;
    m_responseReady = false;
    m_frameDirty = true;
    m_inFlight = false;
    m_timeout.stop();
    m_activeBackend = m_attempt;
    if (m_header.contains("expressions")) {
        QStringList expressions;
        for (const auto &name : m_header.value("expressions").toArray())
            expressions.append(name.toString());
        QStringList motionGroups;
        for (const auto &value : m_header["motionGroups"].toArray())
            motionGroups.append(value.toString());
        const auto hotkeys = m_header["hotkeys"].toArray();
        const bool inputChanged = hotkeys != m_hotkeys || motionGroups != m_motionGroups;
        if (inputChanged) {
            m_motionGroups = motionGroups;
            m_hotkeys = hotkeys;
            m_modelInput.setBindings(hotkeys);
        }
        if (!m_ready || expressions != m_expressions || inputChanged)
            setStatus(true, {}, expressions);
    }
    update();
    // Start preparing one next frame immediately, overlapping presentation.
    queueFrame();
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
        QSGTexture *texture = nullptr;
#ifdef LIVE2D_RHI_TEXTURE
        // Use Qt's own graphics device for presentation, independently of the
        // character helper's Vulkan/OpenGL device. Upload into one allocation.
        auto *rhi = static_cast<QRhi *>(
            window()->rendererInterface()->getResource(window(), QSGRendererInterface::RhiResource));
        if (rhi) {
            auto *current = dynamic_cast<FrameTexture *>(node->texture);
            if (current && current->textureSize() == m_frame.size()) {
                current->setImage(m_frame);
                texture = current;
            } else {
                texture = FrameTexture::create(rhi, m_frame).release();
            }
        }
#endif
        if (!texture)
            texture = window()->createTextureFromImage(m_frame);
        if (!texture) {
            delete node;
            return nullptr;
        }
        if (texture != node->texture) {
            node->image->setTexture(texture);
            delete node->texture;
            node->texture = texture;
        } else {
            node->image->markDirty(QSGNode::DirtyMaterial);
        }
        if (m_trace) {
            const auto consumed = m_scheduleClock.nsecsElapsed();
            const double gap = m_traceConsumed ? (consumed - m_traceConsumed) / 1e6 : 0;
            m_traceConsumed = consumed;
            const double roundtrip = (m_displayReceived - m_displayRequest) / 1e6;
            const double syncWait = (consumed - m_displayReceived) / 1e6;
            const auto timings = m_displayHeader.value("timing").toObject();
            const auto ipc = m_displayHeader.value("ipc").toObject();
            const auto readbackDetail = m_displayHeader.value("readbackDetail").toObject();
            ++m_traceFrames;
            const bool slow = gap > (m_frameRate > 0 ? 1000.0 / m_frameRate : 16.67) + 12 || roundtrip > 25 ||
                              syncWait > 25 || m_displayTimerLate > 12000000 ||
                              ipc.value("previous_write_ms").toDouble() > 25;
            if (slow || consumed - m_traceReport > 1000000000) {
                const auto late = m_displayTimerLate / 1e6;
                QMetaObject::invokeMethod(
                    this,
                    [this, consumed, gap, roundtrip, syncWait, timings, ipc, readbackDetail, late] {
                        qWarning().nospace()
                            << "Live2D trace item=" << this << " backend=" << m_activeBackend
                            << " fps=" << m_frameRate << " frames=" << m_traceFrames << " gap_ms=" << gap
                            << " timer_late_ms=" << late << " response_ms=" << roundtrip
                            << " sync_wait_ms=" << syncWait
                            << " gui_resume_ms=" << (m_scheduleClock.nsecsElapsed() - consumed) / 1e6
                            << " gui_lag_ms=" << m_traceGuiLag / 1e6
                            << " helper=" << QJsonDocument(timings).toJson(QJsonDocument::Compact)
                            << " ipc=" << QJsonDocument(ipc).toJson(QJsonDocument::Compact)
                            << " readback_detail="
                            << QJsonDocument(readbackDetail).toJson(QJsonDocument::Compact)
                            << " pointer_age_ms=" << (m_tracker ? m_tracker->sampleAgeMs() : -1)
                            << " pointer_delivery_ms=" << (m_tracker ? m_tracker->deliveryMs() : -1);
                        m_traceGuiLag = 0;
                    },
                    Qt::QueuedConnection);
                m_traceReport = consumed;
            }
        }
        m_frameDirty = false;
        // updatePaintNode runs during synchronization while the GUI thread is
        // blocked. Resume requests on that thread after synchronization ends.
        QMetaObject::invokeMethod(this, &Live2DItem::queueFrame, Qt::QueuedConnection);
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
    rate = std::clamp(rate, 0, 240); // Zero means render as fast as the helper completes.
    if (rate == m_frameRate) {
        return;
    }
    m_frameRate = rate;
    m_pacer.reset();
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
    resetRelativePointer();
    refreshTracking();
    refreshInput();
    scheduleFrame();
    if (changed)
        Q_EMIT followCursorChanged();
    Q_EMIT cursorFollowModeChanged();
}
void Live2DItem::setMouseTrackingMode(const QString &mode)
{
    if (mode == m_mouseTrackingMode || (mode != QLatin1String("screen") && mode != QLatin1String("relative")))
        return;
    m_mouseTrackingMode = mode;
    resetRelativePointer();
    refreshTracking();
    refreshInput();
    scheduleFrame();
    Q_EMIT mouseTrackingModeChanged();
    Q_EMIT trackingStatusChanged();
}
void Live2DItem::setRelativeSensitivity(qreal sensitivity)
{
    if (!std::isfinite(sensitivity))
        return;
    sensitivity = std::clamp(sensitivity, 0.1, 5.0);
    if (qFuzzyCompare(sensitivity, m_relativeSensitivity))
        return;
    m_relativeSensitivity = sensitivity;
    Q_EMIT relativeSensitivityChanged();
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
    if (!m_followCursor || m_mouseTrackingMode == QLatin1String("relative") ||
        (m_tracker && m_trackingSubscribed && m_tracker->state().valid) || !std::isfinite(x) ||
        !std::isfinite(y)) {
        return;
    }
    m_gaze = QPointF(std::clamp(x, -1.0, 1.0), std::clamp(y, -1.0, 1.0));
    m_pointer = m_gaze;
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

void Live2DItem::setInputMode(const QString &mode)
{
    if (mode == m_inputMode ||
        (mode != QLatin1String("off") && mode != QLatin1String("focused") && mode != QLatin1String("global")))
        return;
    releaseInput();
    m_inputMode = mode;
    refreshInput();
    Q_EMIT inputModeChanged();
}
QString Live2DItem::inputError() const
{
    return m_keysSubscribed && m_globalInput ? m_globalInput->errorString() : QString();
}
void Live2DItem::refreshInput()
{
    const auto keys = m_modelInput.keys();
    const bool active = canRender() && m_running && m_error.isEmpty();
    const bool subscribeKeys = active && m_inputMode == QLatin1String("global") && !keys.isEmpty();
    const bool subscribeMotion = active && m_followCursor && m_mouseTrackingMode == QLatin1String("relative");
    const bool subscribe = subscribeKeys || subscribeMotion;
    if (!m_globalInput && subscribe) {
        m_globalInput = GlobalInput::instance();
        connect(m_globalInput, &GlobalInput::keyChanged, this, [this](const QString &key, bool down) {
            if (m_keysSubscribed)
                appendActions(m_modelInput.key(key, down));
        });
        connect(m_globalInput, &GlobalInput::pointerMoved, this, [this](const QPointF &delta) {
            if (m_relativeSubscribed && canRender() && m_running && relativeFollowAllowed())
                m_relativePointer.move(delta, m_relativeSensitivity, m_relativeClock.nsecsElapsed() / 1e9);
        });
        connect(m_globalInput, &GlobalInput::pointerReset, this, [this] {
            if (m_relativeSubscribed)
                resetRelativePointer();
        });
        connect(m_globalInput, &GlobalInput::statusChanged, this, [this] {
            if (m_relativeSubscribed && !m_globalInput->motionAvailable())
                resetRelativePointer();
            Q_EMIT inputStatusChanged();
            Q_EMIT trackingStatusChanged();
        });
    }
    if (m_keysSubscribed && !subscribeKeys)
        releaseInput();
    if (m_relativeSubscribed && !subscribeMotion)
        resetRelativePointer();
    m_keysSubscribed = subscribeKeys;
    m_relativeSubscribed = subscribeMotion;
    if (m_inputSubscribed && !subscribe) {
        m_inputSubscribed = false;
        releaseInput();
        m_globalInput->unsubscribe(this);
    } else if (subscribe) {
        m_inputSubscribed = true;
        m_globalInput->subscribe(this, subscribeKeys ? keys : QSet<QString>(), subscribeMotion);
    }
    if (!canRender() || !m_running || m_inputMode == QLatin1String("off"))
        releaseInput();
    Q_EMIT inputStatusChanged();
    Q_EMIT trackingStatusChanged();
}
void Live2DItem::appendActions(const QJsonArray &actions)
{
    for (const auto &action : actions) {
        // Preserve release events when input arrives faster than rendering.
        if (m_actions.size() >= 128) {
            const auto type = action.toObject()["action"].toString();
            if (type == QLatin1String("motion"))
                continue;
            for (int i = m_actions.size() - 1; i >= 0; --i) {
                if (m_actions[i].toObject()["name"] == action.toObject()["name"])
                    m_actions.removeAt(i);
            }
        }
        m_actions.append(action);
    }
    if (!actions.isEmpty())
        scheduleFrame();
}
void Live2DItem::releaseInput() { appendActions(m_modelInput.clear()); }
void Live2DItem::inputButton(const QString &key, bool down)
{
    if (!m_ready || !m_running || m_inputMode == QLatin1String("off"))
        return;
    // Global mode uses one source of events, avoiding duplicate Qt delivery.
    if (m_inputMode == QLatin1String("global") && m_keysSubscribed)
        return;
    if (m_inputMode == QLatin1String("focused") && !hasActiveFocus())
        return;
    appendActions(m_modelInput.key(key, down));
}
void Live2DItem::keyPressEvent(QKeyEvent *event)
{
    const auto key = InputKeys::fromQt(event);
    if (!event->isAutoRepeat() && m_modelInput.keys().contains(key))
        inputButton(key, true);
    event->setAccepted(m_inputMode == QLatin1String("focused") && m_modelInput.keys().contains(key));
}
void Live2DItem::keyReleaseEvent(QKeyEvent *event)
{
    const auto key = InputKeys::fromQt(event);
    if (!event->isAutoRepeat())
        inputButton(key, false);
    event->setAccepted(m_inputMode == QLatin1String("focused") && m_modelInput.keys().contains(key));
}
void Live2DItem::playMotion(const QString &group)
{
    if (!m_ready || !m_running || !m_motionGroups.contains(group))
        return;
    appendActions({QJsonObject{{"action", "motion"}, {"group", group}, {"index", -1}}});
}
