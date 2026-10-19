// Validate Qt Quick's Vulkan presentation independently of Cubism's device.
// clang-format off
#include <vulkan/vulkan.h>
// clang-format on
#include "frametexture.h"
#include "live2ditem.h"
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQuickGraphicsDevice>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QThread>
#include <QVulkanInstance>
#include <memory>
#include <private/qbasicvulkanplatforminstance_p.h>
#include <private/qvulkaninstance_p.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

// The offscreen QPA plugin has no Vulkan platform adapter. Supply Qt's basic
// loader for this test; no surface or desktop connection is created.
class HeadlessVulkan final : public QBasicPlatformVulkanInstance
{
  public:
    explicit HeadlessVulkan(QVulkanInstance *instance) : owner(instance) { loadVulkanLibrary("vulkan", 1); }
    void createOrAdoptInstance() override { initInstance(owner, {}); }
    bool supportsPresent(VkPhysicalDevice, uint32_t, QWindow *) override { return false; }

  private:
    QVulkanInstance *owner;
};

class PresentationTest
{
  public:
    static bool awaitFrame(QGuiApplication &app, Live2DItem *item)
    {
        QElapsedTimer timeout;
        timeout.start();
        while (!item->m_frameDirty && item->errorString().isEmpty() && timeout.elapsed() < 10000) {
            app.processEvents();
            QThread::msleep(2);
        }
        return item->m_frameDirty && item->errorString().isEmpty();
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc != 4) {
        return 1;
    }
    VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    const char *instanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME};
    instanceInfo.enabledExtensionCount = 1;
    instanceInfo.ppEnabledExtensionNames = instanceExtensions;
    instanceInfo.pApplicationInfo = &application;
    VkInstance nativeInstance;
    if (vkCreateInstance(&instanceInfo, nullptr, &nativeInstance) != VK_SUCCESS) {
        return 2;
    }
    int status = 0;
    {
        QVulkanInstance instance;
        instance.setVkInstance(nativeInstance);
        instance.setApiVersion(QVersionNumber(1, 1));
        QVulkanInstancePrivate::get(&instance)->platformInst.reset(new HeadlessVulkan(&instance));
        if (!instance.create()) {
            return 3;
        }
        QRhiVulkanInitParams params;
        params.inst = &instance;
        std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Vulkan, &params));
        if (!rhi) {
            return 4;
        }
        // Upload successive RGBA frames into one allocation and verify alpha.
        // This uses the same texture implementation as the production item.
        {
            QImage pixels(32, 24, QImage::Format_RGBA8888_Premultiplied);
            pixels.fill(QColor(120, 60, 30, 128));
            auto frameTexture = FrameTexture::create(rhi.get(), pixels, QRhiTexture::UsedAsTransferSource);
            if (!frameTexture)
                return 12;
            auto *allocation = frameTexture->rhiTexture();
            for (int frame = 0; frame < 3; ++frame) {
                pixels.fill(frame % 2 ? QColor(30, 60, 120, 64) : QColor(120, 60, 30, 128));
                frameTexture->setImage(pixels);
                QRhiCommandBuffer *command;
                if (rhi->beginOffscreenFrame(&command) != QRhi::FrameOpSuccess)
                    return 13;
                auto *updates = rhi->nextResourceUpdateBatch();
                frameTexture->commitTextureOperations(rhi.get(), updates);
                QRhiReadbackResult readback;
                updates->readBackTexture(QRhiReadbackDescription(allocation), &readback);
                command->resourceUpdate(updates);
                rhi->endOffscreenFrame();
                rhi->finish();
                const QImage actual(reinterpret_cast<const uchar *>(readback.data.constData()), 32, 24,
                                    QImage::Format_RGBA8888_Premultiplied);
                if (allocation != frameTexture->rhiTexture() || actual != pixels)
                    return 14;
            }
        }
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
        QQuickRenderControl control;
        QQuickWindow window(&control);
        window.setVulkanInstance(&instance);
        window.setGraphicsDevice(QQuickGraphicsDevice::fromRhi(rhi.get()));
        window.setGeometry(0, 0, 400, 600);
        window.setColor(Qt::transparent);
        if (!control.initialize()) {
            return 5;
        }
        std::unique_ptr<QRhiTexture> texture(
            rhi->newTexture(QRhiTexture::RGBA8, QSize(400, 600), 1,
                            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!texture->create()) {
            return 6;
        }
        std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget({texture.get()}));
        std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());
        if (!target->create()) {
            return 7;
        }
        window.setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(target.get()));
        window.show();
        auto *item = new Live2DItem(window.contentItem());
        item->setSize(QSizeF(400, 600));
        item->setRenderBackend(QString::fromLocal8Bit(argv[2]));
        item->setSource(QUrl::fromLocalFile(QString::fromLocal8Bit(argv[1])));
        QElapsedTimer timeout;
        timeout.start();
        while (!item->ready() && item->errorString().isEmpty() && timeout.elapsed() < 30000) {
            app.processEvents();
            QThread::msleep(5);
        }
        if (!item->ready()) {
            qCritical() << item->errorString();
            status = 8;
        } else {
            item->setRunning(false);
            const auto render = [&] {
                control.polishItems();
                control.beginFrame();
                control.sync();
                control.render();
                QRhiReadbackResult result;
                auto *updates = rhi->nextResourceUpdateBatch();
                updates->readBackTexture(QRhiReadbackDescription(texture.get()), &result);
                control.commandBuffer()->resourceUpdate(updates);
                control.endFrame();
                rhi->finish();
                return QImage(reinterpret_cast<const uchar *>(result.data.constData()),
                              result.pixelSize.width(), result.pixelSize.height(),
                              QImage::Format_RGBA8888_Premultiplied)
                    .copy();
            };
            const QImage image = render();
            int opaque = 0, transparent = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    opaque += image.pixelColor(x, y).alpha() > 128;
                    transparent += image.pixelColor(x, y).alpha() == 0;
                }
            }
            if (opaque < 5000 || transparent < 5000 || !image.save(QString::fromLocal8Bit(argv[3]))) {
                status = 9;
            }
            qInfo() << "Qt Quick Vulkan presentation, character:" << item->activeBackend()
                    << "opaque:" << opaque << "transparent:" << transparent;
            // Update an existing texture several times, then change its size.
            // This catches stale materials, missed uploads, and unsafe storage
            // reuse which a one-frame smoke test cannot exercise.
            QImage zoomed;
            for (int frame = 0; frame < 4 && !status; ++frame) {
                item->setZoom(frame % 2 ? 1.0 : 1.3);
                if (!PresentationTest::awaitFrame(app, item)) {
                    status = 10;
                    break;
                }
                const auto updated = render();
                if (frame == 0)
                    zoomed = updated;
                if (zoomed == image || updated != (frame % 2 ? image : zoomed))
                    status = 10;
            }
            item->setSize(QSizeF(300, 420));
            if (!PresentationTest::awaitFrame(app, item) || render() == image)
                status = 11;
        }
        delete item;
        window.setRenderTarget({});
        control.invalidate();
    }
    vkDestroyInstance(nativeInstance, nullptr);
    return status;
}
