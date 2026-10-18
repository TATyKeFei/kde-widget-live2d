#pragma once
#include <QSize>

namespace RenderProtocol
{
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
