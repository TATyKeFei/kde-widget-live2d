#pragma once

#include <QtGlobal>
#include <algorithm>

// Absolute animation deadlines, independent of timer wakeup and presentation
// delays. Callers stop scheduling while a completed frame awaits consumption.
class FramePacer
{
  public:
    void reset()
    {
        m_origin = -1;
        m_tick = 0;
    }

    qint64 remaining(qint64 now, int rate) const
    {
        return rate > 0 && m_origin >= 0 ? std::max(qint64(0), deadline(rate) - now) : 0;
    }

    void requested(qint64 now, int rate)
    {
        if (rate <= 0) {
            reset();
        } else if (m_origin < 0) {
            m_origin = now;
            m_tick = 1;
        } else if (now >= deadline(rate)) {
            // Skip missed slots without issuing a catch-up burst. Split whole
            // seconds to avoid multiplying a long uptime in nanoseconds by FPS.
            const auto elapsed = now - m_origin;
            m_tick = std::max(m_tick + 1, elapsed / second * rate + elapsed % second * rate / second + 1);
            if (deadline(rate) <= now)
                ++m_tick;
        }
        // An immediate input frame before the deadline does not shift cadence.
    }

  private:
    qint64 deadline(int rate) const
    {
        return m_origin + m_tick / rate * second + m_tick % rate * second / rate;
    }
    static constexpr qint64 second = 1000000000;
    qint64 m_origin = -1, m_tick = 0;
};
