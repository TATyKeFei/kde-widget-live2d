#pragma once

#include <QImage>
#include <QJsonObject>
#include <QSize>
#include <memory>

// A private device/context owned by one renderer helper process. The Qt Quick
// scene graph receives ordinary premultiplied images and owns its own device.
class GraphicsContext
{
  public:
    virtual ~GraphicsContext() = default;
    virtual void resize(const QSize &size) = 0;
    virtual void begin() = 0;
    // Optional tightly packed RGBA destination owned by the caller. The returned
    // image may reference it and is valid until that storage is reused.
    virtual QImage readback(uchar *destination = nullptr) = 0;
    virtual QJsonObject readbackTiming() const { return {}; }
};

std::unique_ptr<GraphicsContext> createGraphicsContext();

#ifdef LIVE2D_VULKAN
#include <Rendering/Vulkan/CubismClass_Vulkan.hpp>
Live2D::Cubism::Framework::CubismImageVulkan uploadVulkanTexture(const QImage &image);
void destroyVulkanTexture(Live2D::Cubism::Framework::CubismImageVulkan &image);
#endif
