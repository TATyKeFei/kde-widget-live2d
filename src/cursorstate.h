#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <algorithm>
#include <cmath>

namespace CursorTracking
{
enum class Mode { Off, Focused, NotFullscreen, Always };

struct State {
    QPointF position;
    bool normalWindow = false;
    bool fullscreen = false;
    bool valid = false;
};

inline bool parseMode(const QString &name, Mode &mode)
{
    if (name == QLatin1String("off"))
        mode = Mode::Off;
    else if (name == QLatin1String("focused"))
        mode = Mode::Focused;
    else if (name == QLatin1String("notFullscreen"))
        mode = Mode::NotFullscreen;
    else if (name == QLatin1String("always"))
        mode = Mode::Always;
    else
        return false;
    return true;
}

inline bool follows(Mode mode, const State &state)
{
    if (!state.valid)
        return false;
    switch (mode) {
    case Mode::Off:
        return false;
    case Mode::Focused:
        return state.normalWindow;
    case Mode::NotFullscreen:
        return !state.fullscreen;
    case Mode::Always:
        return true;
    }
    return false;
}

// All coordinates are desktop logical pixels, including negative monitor origins.
inline QPointF gaze(const QPointF &pointer, const QPointF &center, const QSizeF &screen)
{
    if (screen.width() <= 0 || screen.height() <= 0 || !std::isfinite(pointer.x()) ||
        !std::isfinite(pointer.y()) || !std::isfinite(center.x()) || !std::isfinite(center.y())) {
        return {};
    }
    return {std::clamp((pointer.x() - center.x()) * 2 / screen.width(), -1.0, 1.0),
            std::clamp((center.y() - pointer.y()) * 2 / screen.height(), -1.0, 1.0)};
}

// MousePosition mappings use the character's whole monitor, independent of the
// widget's placement. This gives both directions their full range near edges.
inline QPointF screenPosition(const QPointF &pointer, const QRectF &screen)
{
    return gaze(pointer, screen.center(), screen.size());
}
} // namespace CursorTracking
