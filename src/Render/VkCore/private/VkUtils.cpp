#include "../public/VkUtils.hpp"
#include "../public/VkContext.hpp"
#include <fstream>
#include <stdexcept>

namespace vkcore
{
namespace VkUtils
{

// ==================== 格式相关 ====================

vk::Format findSupportedFormat(vk::PhysicalDevice physicalDevice, const std::vector<vk::Format> &candidates,
                               vk::ImageTiling tiling, vk::FormatFeatureFlags features)
{
    for (vk::Format format : candidates)
    {
        vk::FormatProperties props = physicalDevice.getFormatProperties(format);

        if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features)
        {
            return format;
        }
        else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }

    throw std::runtime_error("failed to find supported format");
}

vk::Format findDepthFormat(vk::PhysicalDevice physicalDevice)
{
    return findSupportedFormat(physicalDevice,
                               {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
                               vk::ImageTiling::eOptimal, vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

bool hasDepthComponent(vk::Format format)
{
    return format == vk::Format::eD16Unorm || format == vk::Format::eD32Sfloat ||
           format == vk::Format::eD16UnormS8Uint || format == vk::Format::eD24UnormS8Uint ||
           format == vk::Format::eD32SfloatS8Uint;
}

bool hasStencilComponent(vk::Format format)
{
    return format == vk::Format::eS8Uint || format == vk::Format::eD16UnormS8Uint ||
           format == vk::Format::eD24UnormS8Uint || format == vk::Format::eD32SfloatS8Uint;
}

uint32_t getFormatSize(vk::Format format)
{
    switch (format)
    {
    case vk::Format::eR8Unorm:
    case vk::Format::eR8Snorm:
    case vk::Format::eR8Uint:
    case vk::Format::eR8Sint:
        return 1;
    case vk::Format::eR8G8Unorm:
    case vk::Format::eR8G8Snorm:
    case vk::Format::eR8G8Uint:
    case vk::Format::eR8G8Sint:
    case vk::Format::eR16Unorm:
    case vk::Format::eR16Snorm:
    case vk::Format::eR16Uint:
    case vk::Format::eR16Sint:
    case vk::Format::eR16Sfloat:
        return 2;
    case vk::Format::eR8G8B8Unorm:
    case vk::Format::eR8G8B8Snorm:
    case vk::Format::eR8G8B8Uint:
    case vk::Format::eR8G8B8Sint:
        return 3;
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Snorm:
    case vk::Format::eR8G8B8A8Uint:
    case vk::Format::eR8G8B8A8Sint:
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eR16G16Unorm:
    case vk::Format::eR16G16Snorm:
    case vk::Format::eR16G16Uint:
    case vk::Format::eR16G16Sint:
    case vk::Format::eR16G16Sfloat:
    case vk::Format::eR32Uint:
    case vk::Format::eR32Sint:
    case vk::Format::eR32Sfloat:
        return 4;
    case vk::Format::eR16G16B16A16Unorm:
    case vk::Format::eR16G16B16A16Snorm:
    case vk::Format::eR16G16B16A16Uint:
    case vk::Format::eR16G16B16A16Sint:
    case vk::Format::eR16G16B16A16Sfloat:
    case vk::Format::eR32G32Uint:
    case vk::Format::eR32G32Sint:
    case vk::Format::eR32G32Sfloat:
        return 8;
    case vk::Format::eR32G32B32Uint:
    case vk::Format::eR32G32B32Sint:
    case vk::Format::eR32G32B32Sfloat:
        return 12;
    case vk::Format::eR32G32B32A32Uint:
    case vk::Format::eR32G32B32A32Sint:
    case vk::Format::eR32G32B32A32Sfloat:
        return 16;
    default:
        return 4; // 默认
    }
}

// ==================== 图像布局转换 ====================

void getLayoutAccessMaskAndStage(vk::ImageLayout layout, vk::AccessFlags &accessMask, vk::PipelineStageFlags &stage)
{
    switch (layout)
    {
    case vk::ImageLayout::eUndefined:
        accessMask = {};
        stage = vk::PipelineStageFlagBits::eTopOfPipe;
        break;
    case vk::ImageLayout::eGeneral:
        accessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        stage = vk::PipelineStageFlagBits::eComputeShader;
        break;
    case vk::ImageLayout::eColorAttachmentOptimal:
        accessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
        stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        break;
    case vk::ImageLayout::eDepthStencilAttachmentOptimal:
        accessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        stage = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
        break;
    case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
        accessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead;
        stage = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
        break;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        accessMask = vk::AccessFlagBits::eShaderRead;
        stage = vk::PipelineStageFlagBits::eFragmentShader;
        break;
    case vk::ImageLayout::eTransferSrcOptimal:
        accessMask = vk::AccessFlagBits::eTransferRead;
        stage = vk::PipelineStageFlagBits::eTransfer;
        break;
    case vk::ImageLayout::eTransferDstOptimal:
        accessMask = vk::AccessFlagBits::eTransferWrite;
        stage = vk::PipelineStageFlagBits::eTransfer;
        break;
    case vk::ImageLayout::ePresentSrcKHR:
        accessMask = {};
        stage = vk::PipelineStageFlagBits::eBottomOfPipe;
        break;
    default:
        accessMask = {};
        stage = vk::PipelineStageFlagBits::eTopOfPipe;
        break;
    }
}

void transitionImageLayout(vk::CommandBuffer commandBuffer, const ImageLayoutTransition &transition)
{
    vk::AccessFlags srcAccessMask, dstAccessMask;
    vk::PipelineStageFlags srcStage, dstStage;

    getLayoutAccessMaskAndStage(transition.oldLayout, srcAccessMask, srcStage);
    getLayoutAccessMaskAndStage(transition.newLayout, dstAccessMask, dstStage);

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = transition.oldLayout;
    barrier.newLayout = transition.newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = transition.image;
    barrier.subresourceRange.aspectMask = transition.aspectMask;
    barrier.subresourceRange.baseMipLevel = transition.baseMipLevel;
    barrier.subresourceRange.levelCount = transition.levelCount;
    barrier.subresourceRange.baseArrayLayer = transition.baseArrayLayer;
    barrier.subresourceRange.layerCount = transition.layerCount;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;

    commandBuffer.pipelineBarrier(srcStage, dstStage, {}, nullptr, nullptr, barrier);
}

// ==================== 命令缓冲区辅助 ====================

vk::CommandBuffer beginSingleTimeCommands(vk::Device device, vk::CommandPool commandPool)
{
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer commandBuffer = device.allocateCommandBuffers(allocInfo)[0];

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    commandBuffer.begin(beginInfo);
    return commandBuffer;
}

void endSingleTimeCommands(vk::Device device, vk::CommandPool commandPool, vk::CommandBuffer commandBuffer,
                           vk::Queue queue)
{
    commandBuffer.end();

    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    queue.submit(submitInfo, nullptr);
    queue.waitIdle();

    device.freeCommandBuffers(commandPool, commandBuffer);
}

// ==================== 内存相关 ====================

uint32_t findMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties)
{
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type");
}

// ==================== 调试与日志 ====================

std::string vkResultToString(vk::Result result)
{
    return vk::to_string(result);
}

void setDebugObjectName(VkContext &context, vk::Device device, uint64_t objectHandle, vk::ObjectType objectType,
                        const std::string &name)
{
    if (!context.m_pfnSetDebugUtilsObjectName)
        return;

    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = static_cast<VkObjectType>(objectType);
    nameInfo.objectHandle = objectHandle;
    nameInfo.pObjectName = name.c_str();

    context.m_pfnSetDebugUtilsObjectName(static_cast<VkDevice>(device), &nameInfo);
}

void insertDebugLabel(VkContext &context, vk::CommandBuffer commandBuffer, const std::string &name,
                      const float color[4])
{
    if (!context.m_pfnCmdInsertDebugUtilsLabel)
        return;

    VkDebugUtilsLabelEXT labelInfo{};
    labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    labelInfo.pLabelName = name.c_str();
    labelInfo.color[0] = color[0];
    labelInfo.color[1] = color[1];
    labelInfo.color[2] = color[2];
    labelInfo.color[3] = color[3];

    context.m_pfnCmdInsertDebugUtilsLabel(static_cast<VkCommandBuffer>(commandBuffer), &labelInfo);
}

void beginDebugLabel(VkContext &context, vk::CommandBuffer commandBuffer, const std::string &name, const float color[4])
{
    if (!context.m_pfnCmdBeginDebugUtilsLabel)
        return;

    VkDebugUtilsLabelEXT labelInfo{};
    labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    labelInfo.pLabelName = name.c_str();
    labelInfo.color[0] = color[0];
    labelInfo.color[1] = color[1];
    labelInfo.color[2] = color[2];
    labelInfo.color[3] = color[3];

    context.m_pfnCmdBeginDebugUtilsLabel(static_cast<VkCommandBuffer>(commandBuffer), &labelInfo);
}

void endDebugLabel(VkContext &context, vk::CommandBuffer commandBuffer)
{
    if (!context.m_pfnCmdEndDebugUtilsLabel)
        return;

    context.m_pfnCmdEndDebugUtilsLabel(static_cast<VkCommandBuffer>(commandBuffer));
}

// ==================== 图像操作 ====================

void copyBufferToImage(vk::CommandBuffer commandBuffer, vk::Buffer buffer, vk::Image image, uint32_t width,
                       uint32_t height, uint32_t layerCount)
{
    vk::BufferImageCopy region;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = layerCount;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);
}

void copyImageToBuffer(vk::CommandBuffer commandBuffer, vk::Image image, vk::Buffer buffer, uint32_t width,
                       uint32_t height, uint32_t layerCount)
{
    vk::BufferImageCopy region;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = layerCount;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    commandBuffer.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, buffer, region);
}

void generateMipmaps(vk::CommandBuffer commandBuffer, vk::Image image, vk::Format format, int32_t width, int32_t height,
                     uint32_t mipLevels)
{
    int32_t mipWidth = width;
    int32_t mipHeight = height;

    for (uint32_t i = 1; i < mipLevels; i++)
    {
        vk::ImageMemoryBarrier barrier;
        barrier.image = image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;

        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {},
                                      nullptr, nullptr, barrier);

        vk::ImageBlit blit;
        blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.srcOffsets[1] = vk::Offset3D{mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.dstOffsets[1] = vk::Offset3D{mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        commandBuffer.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image,
                                vk::ImageLayout::eTransferDstOptimal, blit, vk::Filter::eLinear);

        barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
                                      {}, nullptr, nullptr, barrier);

        if (mipWidth > 1)
            mipWidth /= 2;
        if (mipHeight > 1)
            mipHeight /= 2;
    }

    vk::ImageMemoryBarrier barrier;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {},
                                  nullptr, nullptr, barrier);
}

// ==================== 缓冲区操作 ====================

void copyBuffer(vk::CommandBuffer commandBuffer, vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size)
{
    vk::BufferCopy copyRegion;
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    commandBuffer.copyBuffer(srcBuffer, dstBuffer, copyRegion);
}

// ==================== Shader编译辅助 ====================

std::vector<uint32_t> loadSPIRV(const std::string &filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        throw std::runtime_error("failed to open shader file: " + filename);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    file.seekg(0);
    file.read(reinterpret_cast<char *>(buffer.data()), fileSize);
    file.close();

    return buffer;
}

std::vector<uint32_t> compileGLSLToSPIRV(const std::string &source, vk::ShaderStageFlagBits shaderType,
                                         const std::string &filename)
{
    // 这里需要集成shaderc库来编译GLSL
    // 暂时抛出异常提示用户需要实现
    throw std::runtime_error("compileGLSLToSPIRV not implemented yet. Please use pre-compiled SPIR-V or integrate "
                             "shaderc library.");
}

// ==================== 验证与错误检查 ====================

void checkVkResult(vk::Result result, const std::string &message)
{
    if (result != vk::Result::eSuccess)
    {
        throw std::runtime_error(message + ": " + vk::to_string(result));
    }
}

bool isValidationLayerSupported(const std::string &layerName)
{
    auto availableLayers = vk::enumerateInstanceLayerProperties();
    for (const auto &layerProperties : availableLayers)
    {
        if (layerName == layerProperties.layerName)
        {
            return true;
        }
    }
    return false;
}

bool isDeviceExtensionSupported(vk::PhysicalDevice physicalDevice, const std::string &extensionName)
{
    auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    for (const auto &extension : availableExtensions)
    {
        if (extensionName == extension.extensionName)
        {
            return true;
        }
    }
    return false;
}

// ==================== 对齐辅助 ====================

size_t getUniformBufferAlignment(vk::PhysicalDevice physicalDevice)
{
    vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
    return static_cast<size_t>(properties.limits.minUniformBufferOffsetAlignment);
}

size_t getStorageBufferAlignment(vk::PhysicalDevice physicalDevice)
{
    vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
    return static_cast<size_t>(properties.limits.minStorageBufferOffsetAlignment);
}

} // namespace VkUtils
} // namespace vkcore
