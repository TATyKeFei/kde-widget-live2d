#pragma once
#include <QSize>
#include <time.h>

namespace RenderProtocol
{
// Linux monotonic epoch is shared across processes, unlike QElapsedTimer origins.
inline qint64 monotonicNs()
{
    timespec value{};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return qint64(value.tv_sec) * 1000000000 + value.tv_nsec;
}
inline qint64 threadCpuNs()
{
    timespec value{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value);
    return qint64(value.tv_sec) * 1000000000 + value.tv_nsec;
}
constexpr int maxDimension = 2048;
constexpr int maxPixels = 2048 * 2048;
constexpr int maxHeader = 64 * 1024;
constexpr int maxFrame = maxPixels * 4;
inline bool validSize(const QSize &size)
{
    return size.width() > 0 && size.height() > 0 && size.width() <= maxDimension &&
           size.height() <= maxDimension && qint64(size.width()) * size.height() <= maxPixels;
}
} // namespace RenderProtocol
