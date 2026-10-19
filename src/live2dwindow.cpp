#include "live2dwindow.h"
#include <LayerShellQt/Window>
#include <QCloseEvent>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <algorithm>
#include <cmath>

Live2DWindow::Live2DWindow(QWindow *parent) : QQuickWindow(parent)
{
    setColor(Qt::transparent);
    setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    auto *layer = LayerShellQt::Window::get(this);
    layer->setScope(QStringLiteral("org.kde.plasma.live2d.overlay"));
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setAnchors(LayerShellQt::Window::Anchors(LayerShellQt::Window::AnchorTop) |
                      LayerShellQt::Window::AnchorLeft);
    layer->setExclusiveZone(-1); // Cover no work area, position relative to the complete screen.
    layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
    layer->setActivateOnShow(false);
    connect(this, &QWindow::widthChanged, this, &Live2DWindow::applyPosition);
    connect(this, &QWindow::heightChanged, this, &Live2DWindow::applyPosition);
    connect(this, &QWindow::screenChanged, this, &Live2DWindow::watchScreen);
    watchScreen(screen());
}

void Live2DWindow::setAnchorItem(QQuickItem *item)
{
    if (m_anchor == item)
        return;
    m_anchor = item;
    m_placed = false;
    if (isVisible())
        placeAtAnchor();
    Q_EMIT anchorItemChanged();
}

void Live2DWindow::setClickThrough(bool enabled)
{
    if (m_clickThrough == enabled)
        return;
    m_clickThrough = enabled;
    // Let the window system route input to the surface underneath. Ignoring
    // QML mouse events alone does not pass them through a Wayland surface.
    setFlag(Qt::WindowTransparentForInput, enabled);
    setFlag(Qt::WindowDoesNotAcceptFocus, enabled);
    LayerShellQt::Window::get(this)->setKeyboardInteractivity(
        enabled ? LayerShellQt::Window::KeyboardInteractivityNone
                : LayerShellQt::Window::KeyboardInteractivityOnDemand);
    update(); // Commit input-region/layer changes even while animation is paused.
    Q_EMIT clickThroughChanged();
}

void Live2DWindow::showAtAnchor()
{
    setTransientParent(nullptr);
    placeAtAnchor();
    show();
}

void Live2DWindow::closeEvent(QCloseEvent *event)
{
    event->ignore();
    // The applet unloads this window on return. Finish QWindow's event before
    // allowing QML to destroy it, including when a screen was disconnected.
    QTimer::singleShot(0, this, &Live2DWindow::returnToDesktop);
}

bool Live2DWindow::event(QEvent *event)
{
    if (event->type() == QEvent::Show) {
        // A window declared inside an applet may acquire its desktop window as
        // a transient parent. The overlay has an independent layer-shell role.
        setTransientParent(nullptr);
        if (!m_placed)
            placeAtAnchor();
    }
    return QQuickWindow::event(event);
}

void Live2DWindow::watchScreen(QScreen *current)
{
    disconnect(m_screenGeometry);
    if (current)
        m_screenGeometry = connect(current, &QScreen::geometryChanged, this, &Live2DWindow::applyPosition);
    if (m_placed)
        applyPosition();
}

void Live2DWindow::placeAtAnchor()
{
    auto *output = m_anchor && m_anchor->window() ? m_anchor->window()->screen() : screen();
    if (!output)
        output = QGuiApplication::primaryScreen();
    if (!output)
        return;
    setScreen(output);
    LayerShellQt::Window::get(this)->setScreen(output);
    m_offset = m_anchor && m_anchor->window()
                   ? m_anchor->mapToGlobal(QPointF()).toPoint() - output->geometry().topLeft()
                   : QPoint(output->geometry().width() - width(), output->geometry().height() - height());
    m_placed = true;
    applyPosition();
}

void Live2DWindow::applyPosition()
{
    if (!m_placed || !screen())
        return;
    const auto size = screen()->geometry().size();
    m_offset.setX(std::clamp(m_offset.x(), 0, std::max(0, size.width() - width())));
    m_offset.setY(std::clamp(m_offset.y(), 0, std::max(0, size.height() - height())));
    auto *layer = LayerShellQt::Window::get(this);
    layer->setDesiredSize(QSize(width(), height()));
    layer->setMargins(QMargins(m_offset.x(), m_offset.y(), 0, 0));
    // Also keep Qt's logical geometry meaningful for gaze coordinates and for
    // headless tests. On Wayland the compositor uses the layer-shell margins.
    setPosition(screen()->geometry().topLeft() + m_offset);
    update(); // Commit changed layer margins even while the model is paused.
}

void Live2DWindow::beginMove() { m_moveStart = m_offset; }
void Live2DWindow::moveFromStart(const QPointF &translation)
{
    if (!std::isfinite(translation.x()) || !std::isfinite(translation.y()))
        return;
    const auto size = screen() ? screen()->geometry().size() : QSize();
    m_offset = m_moveStart +
               QPoint(qRound(std::clamp(translation.x(), -double(size.width()), double(size.width()))),
                      qRound(std::clamp(translation.y(), -double(size.height()), double(size.height()))));
    applyPosition();
}
