#include "graphicscontext.h"
#include "renderprotocol.h"
#include <QDebug>
#include <Rendering/Vulkan/CubismRenderer_Vulkan.hpp>
#include <algorithm>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <vector>

namespace
{
void check(VkResult result, const char *operation)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " (VkResult " + std::to_string(result) + ")");
    }
}

class VulkanContext final : public GraphicsContext
{
  public:
    VulkanContext()
    {
        try {
            initialize();
        } catch (...) {
            cleanup();
            throw;
        }
    }
    ~VulkanContext() override { cleanup(); }

    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags)
    {
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) {
                return i;
            }
        }
        throw std::runtime_error("Vulkan 找不到所需的内存类型。");
    }
    void submit(const std::function<void(VkCommandBuffer)> &record)
    {
        check(vkResetCommandBuffer(transferCommand, 0), "重置 Vulkan 传输命令");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(transferCommand, &begin), "开始 Vulkan 命令");
        record(transferCommand);
        check(vkEndCommandBuffer(transferCommand), "结束 Vulkan 命令");
        check(vkResetFences(device, 1, &transferFence), "重置 Vulkan 传输同步");
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        info.commandBufferCount = 1;
        info.pCommandBuffers = &transferCommand;
        check(vkQueueSubmit(queue, 1, &info, transferFence), "提交 Vulkan 命令");
        check(vkWaitForFences(device, 1, &transferFence, VK_TRUE, UINT64_MAX), "等待 Vulkan 传输");
    }
    void buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer &handle, VkDeviceMemory &memory)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        check(vkCreateBuffer(device, &info, nullptr, &handle), "创建 Vulkan 传输缓冲");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, handle, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex =
            memoryType(requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) {
            VkPhysicalDeviceMemoryProperties properties;
            vkGetPhysicalDeviceMemoryProperties(physical, &properties);
            // Readback needs CPU reads; the first coherent type may be uncached
            // write-combined PCIe memory, appropriate for uploads but very slow here.
            for (const auto preferred : {VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                               VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                                                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                                         VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                               VK_MEMORY_PROPERTY_HOST_CACHED_BIT)}) {
                bool found = false;
                for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
                    if ((requirements.memoryTypeBits & (1u << i)) &&
                        (properties.memoryTypes[i].propertyFlags & preferred) == preferred) {
                        allocation.memoryTypeIndex = i;
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
            stagingFlags = properties.memoryTypes[allocation.memoryTypeIndex].propertyFlags;
        }
        check(vkAllocateMemory(device, &allocation, nullptr, &memory), "分配 Vulkan 传输内存");
        check(vkBindBufferMemory(device, handle, memory, 0), "绑定 Vulkan 传输内存");
    }
    void resize(const QSize &size) override
    {
        if (size == m_size) {
            return;
        }
        check(vkDeviceWaitIdle(device), "等待 Vulkan 设备");
        destroyTarget();
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {uint32_t(size.width()), uint32_t(size.height()), 1};
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT;
        check(vkCreateImage(device, &info, nullptr, &target), "创建 Vulkan 渲染目标");
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, target, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex =
            memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &targetMemory), "分配 Vulkan 图像内存");
        check(vkBindImageMemory(device, target, targetMemory, 0), "绑定 Vulkan 图像内存");
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = target;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(device, &view, nullptr, &targetView), "创建 Vulkan 图像视图");
        buffer(VkDeviceSize(size.width()) * size.height() * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, staging,
               stagingMemory);
        check(vkMapMemory(device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &stagingPixels),
              "映射 Vulkan 读回内存");
        m_size = size;
        using Renderer = Csm::Rendering::CubismRenderer_Vulkan;
        Renderer::SetConstantSettings(device, physical, pool, queue, 1,
                                      {uint32_t(size.width()), uint32_t(size.height())}, targetView, format,
                                      depthFormat);
        Renderer::EnableChangeRenderTarget();
        Renderer::SetRenderTarget(target, targetView, format,
                                  {uint32_t(size.width()), uint32_t(size.height())});
    }
    void begin() override {} // Cubism clears the target with transparent black.
    QJsonObject readbackTiming() const override { return timing; }
    QImage readback(uchar *destination = nullptr) override
    {
        const auto started = RenderProtocol::monotonicNs();
        submit([&](VkCommandBuffer command) {
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = target;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = {uint32_t(m_size.width()), uint32_t(m_size.height()), 1};
            vkCmdCopyImageToBuffer(command, target, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging, 1, &copy);
            std::swap(barrier.oldLayout, barrier.newLayout);
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &barrier);
            VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            host.buffer = staging;
            host.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                                 nullptr, 1, &host, 0, nullptr);
        });
        if (!(stagingFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            range.memory = stagingMemory;
            range.size = VK_WHOLE_SIZE;
            check(vkInvalidateMappedMemoryRanges(device, 1, &range), "刷新 Vulkan 读回缓存");
        }
        const auto copiedFrom = RenderProtocol::monotonicNs();
        QImage image = destination ? QImage(destination, m_size.width(), m_size.height(), m_size.width() * 4,
                                            QImage::Format_RGBA8888_Premultiplied)
                                   : QImage(m_size, QImage::Format_RGBA8888_Premultiplied);
        if (image.isNull())
            throw std::runtime_error("无法分配 Vulkan 读回图像。");
        std::memcpy(image.bits(), stagingPixels, size_t(image.sizeInBytes()));
        timing = {{"gpu_wait_ms", (copiedFrom - started) / 1e6},
                  {"pixel_copy_ms", (RenderProtocol::monotonicNs() - copiedFrom) / 1e6},
                  {"host_cached", bool(stagingFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)},
                  {"host_coherent", bool(stagingFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)}};
        return image;
    }
    Csm::CubismImageVulkan upload(const QImage &image)
    {
        // Pre-filter the entire texture pyramid once, in premultiplied alpha.
        // CPU generation also works on devices without linear image blits.
        // Sampling only level zero aliases badly when a large atlas is shrunk
        // into a small desktop character (OpenGL already uses mipmaps).
        std::vector<QImage> levels{image};
        while (levels.back().width() > 1 || levels.back().height() > 1) {
            const auto &previous = levels.back();
            levels.push_back(previous.scaled(std::max(1, previous.width() / 2),
                                             std::max(1, previous.height() / 2), Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation));
        }
        VkDeviceSize uploadBytes = 0;
        std::vector<VkBufferImageCopy> copies;
        for (uint32_t level = 0; level < levels.size(); ++level) {
            const auto &mip = levels[level];
            VkBufferImageCopy copy{};
            copy.bufferOffset = uploadBytes;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
            copy.imageExtent = {uint32_t(mip.width()), uint32_t(mip.height()), 1};
            copies.push_back(copy);
            uploadBytes += mip.sizeInBytes();
        }
        const auto mipCount = uint32_t(levels.size());
        VkBuffer uploadBuffer = VK_NULL_HANDLE;
        VkDeviceMemory uploadMemory = VK_NULL_HANDLE;
        Csm::CubismImageVulkan texture;
        try {
            buffer(uploadBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, uploadBuffer, uploadMemory);
            void *mapped = nullptr;
            check(vkMapMemory(device, uploadMemory, 0, uploadBytes, 0, &mapped), "上传 Vulkan 纹理");
            for (size_t level = 0; level < levels.size(); ++level)
                std::memcpy(static_cast<unsigned char *>(mapped) + copies[level].bufferOffset,
                            levels[level].constBits(), size_t(levels[level].sizeInBytes()));
            vkUnmapMemory(device, uploadMemory);
            texture.CreateImage(device, physical, image.width(), image.height(), mipCount, format,
                                VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
            texture.CreateView(device, format, VK_IMAGE_ASPECT_COLOR_BIT, mipCount);
            texture.CreateSampler(device, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_FILTER_LINEAR,
                                  VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, 1, mipCount - 1);
            if (!texture.GetImage() || !texture.GetView() || !texture.GetSampler()) {
                throw std::runtime_error("创建 Vulkan 纹理失败。");
            }
            submit([&](VkCommandBuffer command) {
                texture.SetImageLayout(command, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipCount,
                                       VK_IMAGE_ASPECT_COLOR_BIT);
                vkCmdCopyBufferToImage(command, uploadBuffer, texture.GetImage(),
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copies.size(), copies.data());
                texture.SetImageLayout(command, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipCount,
                                       VK_IMAGE_ASPECT_COLOR_BIT);
            });
        } catch (...) {
            texture.Destroy(device);
            if (uploadBuffer) {
                vkDestroyBuffer(device, uploadBuffer, nullptr);
            }
            if (uploadMemory) {
                vkFreeMemory(device, uploadMemory, nullptr);
            }
            throw;
        }
        vkDestroyBuffer(device, uploadBuffer, nullptr);
        vkFreeMemory(device, uploadMemory, nullptr);
        return texture;
    }
    VkDevice device = VK_NULL_HANDLE;

  private:
    void initialize()
    {
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "Live2D renderer";
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &application;
        check(vkCreateInstance(&instanceInfo, nullptr, &instance), "创建 Vulkan 1.3 实例");
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "枚举 Vulkan 设备");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "枚举 Vulkan 设备");
        // Prefer a GPU over a CPU implementation when both are installed.
        std::stable_sort(devices.begin(), devices.end(), [](auto a, auto b) {
            VkPhysicalDeviceProperties pa, pb;
            vkGetPhysicalDeviceProperties(a, &pa);
            vkGetPhysicalDeviceProperties(b, &pb);
            return pa.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU &&
                   pb.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        });
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_3) {
                continue;
            }
            uint32_t extensionCount = 0;
            check(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr),
                  "枚举 Vulkan 扩展");
            std::vector<VkExtensionProperties> extensions(extensionCount);
            check(
                vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data()),
                "枚举 Vulkan 扩展");
            if (std::none_of(extensions.begin(), extensions.end(), [](const auto &extension) {
                    return std::strcmp(extension.extensionName,
                                       VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME) == 0;
                })) {
                continue;
            }
            VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
            VkPhysicalDeviceVulkan13Features features13{
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            features13.pNext = &dynamic;
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &features13;
            vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (!features13.dynamicRendering || !features13.synchronization2 ||
                !dynamic.extendedDynamicState) {
                continue;
            }
            uint32_t familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            for (uint32_t i = 0; i < familyCount; ++i) {
                if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    physical = candidate;
                    family = i;
                    break;
                }
            }
            if (physical) {
                qInfo() << "Live2D Vulkan:" << properties.deviceName;
                break;
            }
        }
        if (!physical) {
            throw std::runtime_error(
                "需要 Vulkan 1.3、dynamicRendering、synchronization2 和 VK_EXT_extended_dynamic_state。");
        }
        const float priority = 1;
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT dynamic{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
        dynamic.extendedDynamicState = VK_TRUE;
        VkPhysicalDeviceVulkan13Features features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        features.dynamicRendering = features.synchronization2 = VK_TRUE;
        features.pNext = &dynamic;
        const char *extensions[] = {VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME};
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.pNext = &features;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queueInfo;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = extensions;
        check(vkCreateDevice(physical, &info, nullptr, &device), "创建 Vulkan 设备");
        vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = family;
        check(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "创建 Vulkan 命令池");
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = pool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &allocation, &transferCommand), "分配 Vulkan 传输命令");
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        check(vkCreateFence(device, &fenceInfo, nullptr, &transferFence), "创建 Vulkan 传输同步");
        bool foundDepth = false;
        for (auto candidate : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(physical, candidate, &properties);
            if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depthFormat = candidate;
                foundDepth = true;
                break;
            }
        }
        if (!foundDepth) {
            throw std::runtime_error("Vulkan 不支持所需的深度模板格式。");
        }
    }
    void destroyTarget()
    {
        if (targetView) {
            vkDestroyImageView(device, targetView, nullptr);
        }
        if (target) {
            vkDestroyImage(device, target, nullptr);
        }
        if (targetMemory) {
            vkFreeMemory(device, targetMemory, nullptr);
        }
        if (staging) {
            vkDestroyBuffer(device, staging, nullptr);
        }
        if (stagingPixels) {
            vkUnmapMemory(device, stagingMemory);
            stagingPixels = nullptr;
        }
        if (stagingMemory) {
            vkFreeMemory(device, stagingMemory, nullptr);
        }
        targetView = VK_NULL_HANDLE;
        target = VK_NULL_HANDLE;
        targetMemory = VK_NULL_HANDLE;
        staging = VK_NULL_HANDLE;
        stagingMemory = VK_NULL_HANDLE;
    }
    void cleanup()
    {
        if (device) {
            vkDeviceWaitIdle(device);
            destroyTarget();
            if (transferFence)
                vkDestroyFence(device, transferFence, nullptr);
            if (pool) {
                vkDestroyCommandPool(device, pool, nullptr);
            }
            vkDestroyDevice(device, nullptr);
        }
        if (instance) {
            vkDestroyInstance(instance, nullptr);
        }
    }
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer transferCommand = VK_NULL_HANDLE;
    VkFence transferFence = VK_NULL_HANDLE;
    void *stagingPixels = nullptr;
    VkMemoryPropertyFlags stagingFlags = 0;
    QJsonObject timing;
    VkImage target = VK_NULL_HANDLE;
    VkImageView targetView = VK_NULL_HANDLE;
    VkDeviceMemory targetMemory = VK_NULL_HANDLE, stagingMemory = VK_NULL_HANDLE;
    VkBuffer staging = VK_NULL_HANDLE;
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    QSize m_size;
};
VulkanContext *activeContext = nullptr;
} // namespace
std::unique_ptr<GraphicsContext> createGraphicsContext()
{
    auto context = std::make_unique<VulkanContext>();
    activeContext = context.get();
    return context;
}
Csm::CubismImageVulkan uploadVulkanTexture(const QImage &image) { return activeContext->upload(image); }
void destroyVulkanTexture(Csm::CubismImageVulkan &image) { image.Destroy(activeContext->device); }
