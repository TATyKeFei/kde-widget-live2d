#pragma once

#include "cursorstate.h"
#include "framepacer.h"
#include "modelinputstate.h"
#include "relativepointer.h"
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <memory>

class DesktopTracker;
class GlobalInput;
class RenderWorker;

class Live2DItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(int frameRate READ frameRate WRITE setFrameRate NOTIFY frameRateChanged)
    Q_PROPERTY(qreal zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(bool followCursor READ followCursor WRITE setFollowCursor NOTIFY followCursorChanged)
    Q_PROPERTY(QString cursorFollowMode READ cursorFollowMode WRITE setCursorFollowMode NOTIFY
                   cursorFollowModeChanged)
    Q_PROPERTY(QString trackingError READ trackingError NOTIFY trackingStatusChanged)
    Q_PROPERTY(QString mouseTrackingMode READ mouseTrackingMode WRITE setMouseTrackingMode NOTIFY
                   mouseTrackingModeChanged)
    Q_PROPERTY(qreal relativeSensitivity READ relativeSensitivity WRITE setRelativeSensitivity NOTIFY
                   relativeSensitivityChanged)
    Q_PROPERTY(QString idleGroup READ idleGroup WRITE setIdleGroup NOTIFY idleGroupChanged)
    Q_PROPERTY(QString renderBackend READ renderBackend WRITE setRenderBackend NOTIFY renderBackendChanged)
    Q_PROPERTY(QString activeBackend READ activeBackend NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY statusChanged)
    Q_PROPERTY(QStringList expressions READ expressions NOTIFY statusChanged)
    Q_PROPERTY(QStringList motionGroups READ motionGroups NOTIFY statusChanged)
    Q_PROPERTY(QString inputMode READ inputMode WRITE setInputMode NOTIFY inputModeChanged)
    Q_PROPERTY(QString inputError READ inputError NOTIFY inputStatusChanged)

  public:
    explicit Live2DItem(QQuickItem *parent = nullptr);
    ~Live2DItem() override;
    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    bool running() const { return m_running; }
    void setRunning(bool running);
    int frameRate() const { return m_frameRate; }
    void setFrameRate(int rate);
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    bool followCursor() const { return m_followCursor; }
    void setFollowCursor(bool follow);
    QString cursorFollowMode() const { return m_cursorFollowMode; }
    void setCursorFollowMode(const QString &mode);
    QString trackingError() const;
    QString mouseTrackingMode() const { return m_mouseTrackingMode; }
    void setMouseTrackingMode(const QString &mode);
    qreal relativeSensitivity() const { return m_relativeSensitivity; }
    void setRelativeSensitivity(qreal sensitivity);
    QString idleGroup() const { return m_idleGroup; }
    void setIdleGroup(const QString &group);
    QString renderBackend() const { return m_backend; }
    void setRenderBackend(const QString &backend);
    QString activeBackend() const { return m_activeBackend; }
    bool ready() const { return m_ready; }
    QString errorString() const { return m_error; }
    QStringList expressions() const { return m_expressions; }
    QStringList motionGroups() const { return m_motionGroups; }
    QString inputMode() const { return m_inputMode; }
    void setInputMode(const QString &mode);
    QString inputError() const;
    Q_INVOKABLE void setGaze(qreal x, qreal y);
    Q_INVOKABLE void tap(qreal x, qreal y);
    Q_INVOKABLE void setExpression(const QString &name);
    Q_INVOKABLE void playMotion(const QString &group);
    Q_INVOKABLE void inputButton(const QString &key, bool down);
    Q_INVOKABLE void releaseInput();
    Q_INVOKABLE void reload();

  Q_SIGNALS:
    void sourceChanged();
    void runningChanged();
    void frameRateChanged();
    void zoomChanged();
    void followCursorChanged();
    void cursorFollowModeChanged();
    void trackingStatusChanged();
    void mouseTrackingModeChanged();
    void relativeSensitivityChanged();
    void idleGroupChanged();
    void renderBackendChanged();
    void statusChanged();
    void inputModeChanged();
    void inputStatusChanged();

  protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void componentComplete() override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

  private:
    void refreshTimer();
    void queueFrame();
    void requestFrame();
    void scheduleFrame();
    void readFrames();
    void stopRenderer();
    void failRenderer(const QString &error);
    void setStatus(bool ready, const QString &error, const QStringList &expressions = {});
    bool canRender() const;
    void refreshTracking();
    void refreshInput();
    void appendActions(const QJsonArray &actions);
    bool updateDesktopGaze();
    bool relativeFollowAllowed() const;
    void resetRelativePointer();
    QMetaObject::Connection m_windowVisibilityConnection;
    QMetaObject::Connection m_windowActiveConnection;
    QUrl m_source;
    QTimer m_timer, m_timeout;
    QElapsedTimer m_clock;
    QElapsedTimer m_scheduleClock;
    FramePacer m_pacer;
    bool m_trace = false;
    qint64 m_traceRequest = 0, m_traceReceived = 0, m_traceConsumed = 0;
    qint64 m_traceTimerDue = 0, m_traceTimerLate = 0, m_traceHeartbeat = 0, m_traceGuiLag = 0;
    qint64 m_traceReport = 0;
    quint64 m_traceFrames = 0;
    RenderWorker *m_worker = nullptr;
    quint64 m_generation = 0;
    bool m_rendererStarting = false, m_rendererStarted = false;
    QImage m_pendingFrame;
    bool m_running = true, m_followCursor = true, m_ready = false;
    bool m_inFlight = false, m_fellBack = false, m_frameDirty = false, m_requestPending = false;
    int m_frameRate = 30;
    qreal m_zoom = 1;
    QString m_idleGroup = QStringLiteral("Idle");
    QString m_backend = QStringLiteral("auto"), m_attempt, m_activeBackend;
    QString m_error, m_vulkanError;
    QStringList m_expressions;
    QStringList m_motionGroups;
    QString m_inputMode = QStringLiteral("focused");
    ModelInputState m_modelInput;
    QJsonArray m_hotkeys, m_actions;
    QPointer<GlobalInput> m_globalInput;
    bool m_inputSubscribed = false, m_keysSubscribed = false, m_relativeSubscribed = false;
    QString m_mouseTrackingMode = QStringLiteral("screen");
    qreal m_relativeSensitivity = 1;
    RelativePointer m_relativePointer;
    QElapsedTimer m_relativeClock;
    QPointF m_gaze, m_pointer;
    QPointer<DesktopTracker> m_tracker;
    bool m_trackingSubscribed = false;
    QString m_cursorFollowMode = QStringLiteral("always");
    CursorTracking::Mode m_followMode = CursorTracking::Mode::Always;
    QList<QPointF> m_taps;
    QString m_expression;
    QJsonObject m_header;
    QJsonObject m_displayHeader;
    qint64 m_displayRequest = 0, m_displayReceived = 0, m_displayTimerLate = 0;
    bool m_responseReady = false;
    QImage m_frame;
    friend class ItemTest;
    friend class PresentationTest;
};
