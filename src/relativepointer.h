#pragma once

#include <QPointF>
#include <algorithm>
#include <cmath>

// A bounded virtual pointer for locked-cursor games. Time is monotonic seconds,
// independent of the model/render clock, and deltas are unaccelerated evdev counts.
class RelativePointer
{
  public:
    void reset()
    {
        m_position = {};
        m_lastMotion = m_updated = -1;
    }

    QPointF position(double now)
    {
        if (!std::isfinite(now))
            return m_position;
        if (m_lastMotion >= 0) {
            // Keep the last pose briefly, then return smoothly to the center.
            const double decayTime = now - std::max(m_updated, m_lastMotion + 0.12);
            if (decayTime > 0)
                m_position *= std::exp(-decayTime / 0.25);
        }
        m_updated = now;
        return m_position;
    }

    void move(const QPointF &delta, double sensitivity, double now)
    {
        if (!std::isfinite(delta.x()) || !std::isfinite(delta.y()) || !std::isfinite(sensitivity) ||
            !std::isfinite(now) || delta.isNull())
            return;
        position(now);
        const double scale = std::clamp(sensitivity, 0.1, 5.0) / 500;
        m_position.setX(std::clamp(m_position.x() + delta.x() * scale, -1.0, 1.0));
        m_position.setY(std::clamp(m_position.y() - delta.y() * scale, -1.0, 1.0));
        m_lastMotion = now;
    }

  private:
    QPointF m_position;
    double m_lastMotion = -1, m_updated = -1;
};
