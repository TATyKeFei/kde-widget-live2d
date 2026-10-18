#pragma once

#include <QImage>
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
    virtual QImage readback() = 0;
};

std::unique_ptr<GraphicsContext> createGraphicsContext();

#ifdef LIVE2D_VULKAN
#include <Rendering/Vulkan/CubismClass_Vulkan.hpp>
Live2D::Cubism::Framework::CubismImageVulkan uploadVulkanTexture(const QImage &image);
void destroyVulkanTexture(Live2D::Cubism::Framework::CubismImageVulkan &image);
#endif
