#include "../public/TransferManager.hpp"
#include "../public/VkUtils.hpp"
#include "TransferManager.hpp"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <unordered_map>

using namespace vkcore;

TransferManager::~TransferManager()
{
    cleanup();
}

void TransferManager::initialize(VkContext &ctx, VkResourceAllocator &allocator, const TransferManagerConfig &config)
{
    if (m_ctx)
        return;

    m_ctx = &ctx;
    m_allocator = &allocator;
    m_config = config;
}

void TransferManager::cleanup()
{
    if (!m_ctx)
        return;

    std::lock_guard<std::mutex> lock(m_resourcesMutex);
    auto device = m_ctx->getDevice();

    for (auto &resources : m_threadResources)
    {
        // 销毁staging buffers
        for (auto &info : resources->stagingBufferPool)
        {
            info.buffer.release();
        }
        resources->stagingBufferPool.clear();

        if (resources->transferCommandPool)
        {
            device.destroyCommandPool(resources->transferCommandPool);
        }
        if (resources->graphicsCommandPool)
        {
            device.destroyCommandPool(resources->graphicsCommandPool);
        }

        // 先等待所有activeSubmissions完成
        if (!resources->activeSubmissions.empty())
        {
            std::vector<vk::Fence> fences;
            for (const auto &sub : resources->activeSubmissions)
            {
                fences.push_back(sub.tokenState->fence);
            }
            if (device.waitForFences(fences, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
            {
                // Log warning?
            }

            // 释放提交关联的资源
            for (const auto &sub : resources->activeSubmissions)
            {
                for (size_t idx : sub.stagingBuffersToRelease)
                {
                    if (idx < resources->stagingBufferPool.size())
                        resources->stagingBufferPool[idx].inUse = false;
                }
                device.freeCommandBuffers(sub.commandPool, sub.cmdBuffer);
            }
        }

        // 销毁所有fences
        for (auto fence : resources->fencePool)
        {
            device.destroyFence(fence);
        }
        for (const auto &sub : resources->activeSubmissions)
        {
            device.destroyFence(sub.tokenState->fence);
        }
        resources->activeSubmissions.clear();
        resources->fencePool.clear();
    }
    m_threadResources.clear();

    m_ctx = nullptr;
    m_allocator = nullptr;
}

TransferToken TransferManager::uploadToBuffer(const ManagedBuffer &dstBuffer, const void *data, vk::DeviceSize size,
                                              vk::DeviceSize dstOffset)
{
    if (!m_allocator || !m_ctx)
        throw std::runtime_error("TransferManager is not initialized");
    if (!dstBuffer)
        throw std::runtime_error("Destination buffer is invalid");

    const vk::DeviceSize bufferSize = dstBuffer.getSize();
    if (dstOffset >= bufferSize)
        throw std::out_of_range("dstOffset exceeds destination buffer size");
    if (size > bufferSize - dstOffset)
        throw std::out_of_range("Upload size exceeds destination buffer capacity");

    size_t stagingIndex = acquireStagingBuffer(size);
    copyHostToStaging(stagingIndex, data, static_cast<size_t>(size));

    // 直接录制命令，不调用copyBuffer以控制提交
    vk::CommandBuffer cmd = beginOneTimeCommands(TransferQueueType::Transfer);
    vk::BufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    cmd.copyBuffer(getThreadResources().stagingBufferPool[stagingIndex].buffer.getBuffer(), dstBuffer.getBuffer(), 1,
                   &copyRegion);

    // 提交并传递需要释放的staging buffer
    TransferToken token = endOneTimeCommands(cmd, TransferQueueType::Transfer, {stagingIndex});

    cleanupUnusedStagingBuffers();
    return token;
}

TransferToken TransferManager::uploadToImage(const ManagedImage &dstImage, const void *data, vk::DeviceSize dataSize,
                                             uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel,
                                             uint32_t arrayLayer)
{
    if (!m_allocator || !m_ctx)
        throw std::runtime_error("TransferManager is not initialized");

    size_t stagingIndex = acquireStagingBuffer(dataSize);
    copyHostToStaging(stagingIndex, data, static_cast<size_t>(dataSize));

    vk::CommandBuffer cmd = beginOneTimeCommands(TransferQueueType::Graphics);

    // 转换到TransferDst
    BarrierInfo barrierInfo = getBarrierInfo(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = dstImage.getImage();
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = arrayLayer;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = barrierInfo.srcAccessMask;
    barrier.dstAccessMask = barrierInfo.dstAccessMask;
    cmd.pipelineBarrier(barrierInfo.srcStage, barrierInfo.dstStage, {}, nullptr, nullptr, barrier);

    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, depth};

    cmd.copyBufferToImage(getThreadResources().stagingBufferPool[stagingIndex].buffer.getBuffer(), dstImage.getImage(),
                          vk::ImageLayout::eTransferDstOptimal, 1, &region);

    // 转换到ShaderReadOnly
    barrierInfo = getBarrierInfo(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = barrierInfo.srcAccessMask;
    barrier.dstAccessMask = barrierInfo.dstAccessMask;
    cmd.pipelineBarrier(barrierInfo.srcStage, barrierInfo.dstStage, {}, nullptr, nullptr, barrier);

    TransferToken token = endOneTimeCommands(cmd, TransferQueueType::Graphics, {stagingIndex});

    cleanupUnusedStagingBuffers();
    return token;
}

TransferToken TransferManager::copyBuffer(const ManagedBuffer &srcBuffer, const ManagedBuffer &dstBuffer,
                                          vk::DeviceSize size, vk::DeviceSize srcOffset, vk::DeviceSize dstOffset)
{
    if (!m_ctx)
        throw std::runtime_error("TransferManager is not initialized");

    const vk::DeviceSize dstSize = dstBuffer.getSize();
    if (dstOffset >= dstSize)
        throw std::out_of_range("dstOffset exceeds destination buffer size");
    if (size > dstSize - dstOffset)
        throw std::out_of_range("Copy size exceeds destination buffer capacity");

    vk::CommandBuffer cmd = beginOneTimeCommands(TransferQueueType::Transfer);
    vk::BufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    cmd.copyBuffer(srcBuffer.getBuffer(), dstBuffer.getBuffer(), 1, &copyRegion);
    return endOneTimeCommands(cmd, TransferQueueType::Transfer);
}

void vkcore::TransferManager::writeToUniformBuffer(const ManagedBuffer &dstBuffer, const void *data,
                                                   vk::DeviceSize size, vk::DeviceSize dstOffset)
{
    if (!m_ctx)
        throw std::runtime_error("TransferManager is not initialized");
    if (!dstBuffer)
        throw std::runtime_error("Destination buffer is invalid");
    const vk::DeviceSize bufferSize = dstBuffer.getSize();
    if (dstOffset >= bufferSize)
        throw std::out_of_range("dstOffset exceeds destination buffer size");
    if (size > bufferSize - dstOffset)
        throw std::out_of_range("Write size exceeds destination buffer capacity");
    void *mappedData = nullptr;
    VmaAllocation allocation = dstBuffer.getAllocation();
    vmaMapMemory(m_allocator->getAllocator(), allocation, &mappedData);
    std::memcpy(static_cast<uint8_t *>(mappedData) + dstOffset, data, static_cast<size_t>(size));
    vmaUnmapMemory(m_allocator->getAllocator(), allocation);
}
TransferToken TransferManager::copyBufferToImage(const ManagedBuffer &srcBuffer, const ManagedImage &dstImage,
                                                 uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel,
                                                 uint32_t arrayLayer)
{
    if (!m_ctx)
        throw std::runtime_error("TransferManager is not initialized");

    vk::CommandBuffer cmd = beginOneTimeCommands(TransferQueueType::Graphics);

    BarrierInfo barrierInfo = getBarrierInfo(vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = dstImage.getImage();
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = arrayLayer;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = barrierInfo.srcAccessMask;
    barrier.dstAccessMask = barrierInfo.dstAccessMask;
    cmd.pipelineBarrier(barrierInfo.srcStage, barrierInfo.dstStage, {}, nullptr, nullptr, barrier);

    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, depth};

    cmd.copyBufferToImage(srcBuffer.getBuffer(), dstImage.getImage(), vk::ImageLayout::eTransferDstOptimal, 1, &region);

    barrierInfo = getBarrierInfo(vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = barrierInfo.srcAccessMask;
    barrier.dstAccessMask = barrierInfo.dstAccessMask;
    cmd.pipelineBarrier(barrierInfo.srcStage, barrierInfo.dstStage, {}, nullptr, nullptr, barrier);

    return endOneTimeCommands(cmd, TransferQueueType::Graphics);
}

TransferToken TransferManager::transitionImageLayout(const ManagedImage &image, vk::ImageLayout oldLayout,
                                                     vk::ImageLayout newLayout, vk::ImageAspectFlags aspectMask,
                                                     uint32_t baseMipLevel, uint32_t levelCount,
                                                     uint32_t baseArrayLayer, uint32_t layerCount,
                                                     bool useGraphicsQueue)
{
    if (!m_ctx)
        throw std::runtime_error("TransferManager is not initialized");

    BarrierInfo barrierInfo = getBarrierInfo(oldLayout, newLayout);

    const vk::PipelineStageFlags transferOnlyStages = vk::PipelineStageFlagBits::eTopOfPipe |
                                                      vk::PipelineStageFlagBits::eBottomOfPipe |
                                                      vk::PipelineStageFlagBits::eTransfer;
    const bool requiresGraphicsQueue =
        useGraphicsQueue || ((barrierInfo.srcStage | barrierInfo.dstStage) & ~transferOnlyStages);

    vk::CommandBuffer cmd =
        beginOneTimeCommands(requiresGraphicsQueue ? TransferQueueType::Graphics : TransferQueueType::Transfer);

    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.getImage();
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
    barrier.subresourceRange.layerCount = layerCount;
    barrier.srcAccessMask = barrierInfo.srcAccessMask;
    barrier.dstAccessMask = barrierInfo.dstAccessMask;

    cmd.pipelineBarrier(barrierInfo.srcStage, barrierInfo.dstStage, {}, nullptr, nullptr, barrier);
    return endOneTimeCommands(cmd, requiresGraphicsQueue ? TransferQueueType::Graphics : TransferQueueType::Transfer);
}

TransferToken TransferManager::generateMipmaps(const ManagedImage &image, uint32_t width, uint32_t height,
                                               uint32_t mipLevels)
{
    if (!m_ctx)
        throw std::runtime_error("TransferManager is not initialized");

    // 检查是否支持线性过滤blit
    auto formatProperties = m_ctx->getPhysicalDevice().getFormatProperties(image.getFormat());
    if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear))
    {
        throw std::runtime_error("Image format does not support linear blitting for mipmaps");
    }

    vk::CommandBuffer cmd = beginOneTimeCommands(TransferQueueType::Graphics);
    VkUtils::generateMipmaps(cmd, image.getImage(), image.getFormat(), static_cast<int32_t>(width),
                             static_cast<int32_t>(height), mipLevels);
    return endOneTimeCommands(cmd, TransferQueueType::Graphics);
}

size_t TransferManager::acquireStagingBuffer(vk::DeviceSize size)
{
    if (!m_allocator)
        throw std::runtime_error("TransferManager allocator not set");

    auto &resources = getThreadResources();
    auto &pool = resources.stagingBufferPool;

    if (m_config.enableStagingBufferPool)
    {
        // 找到可用的且容量足够的buffer
        for (size_t i = 0; i < pool.size(); ++i)
        {
            auto &info = pool[i];
            if (!info.inUse && info.size >= size)
            {
                info.inUse = true;
                return i;
            }
        }

        // 需要新建
        if (pool.size() < m_config.maxPooledStagingBuffers)
        {
            vk::DeviceSize allocSize = max(size, static_cast<vk::DeviceSize>(m_config.minStagingBufferSize));
            if (m_config.maxStagingBufferSize > 0)
                allocSize = max(size, min(allocSize, m_config.maxStagingBufferSize));

            StagingBufferInfo info{};
            info.buffer = createStagingBuffer(allocSize);
            info.size = allocSize;
            info.inUse = true;
            pool.push_back(std::move(info));
            return pool.size() - 1;
        }
    }

    // 非池化或池已满，创建独立的buffer
    StagingBufferInfo info{};
    info.buffer = createStagingBuffer(size);
    info.size = size;
    info.inUse = true;
    pool.push_back(std::move(info));
    return pool.size() - 1;
}

void TransferManager::releaseStagingBuffer(size_t index)
{
    auto &resources = getThreadResources();
    if (index < resources.stagingBufferPool.size())
    {
        resources.stagingBufferPool[index].inUse = false;
    }
}

void TransferManager::cleanupUnusedStagingBuffers()
{
    if (!m_config.enableStagingBufferPool)
        return;

    auto &resources = getThreadResources();
    auto &pool = resources.stagingBufferPool;

    // 只保留前maxPooledStagingBuffers个空闲buffer
    for (size_t i = pool.size(); i > 0; --i)
    {
        if (pool.size() <= m_config.maxPooledStagingBuffers)
            break;

        size_t idx = i - 1;
        if (!pool[idx].inUse)
        {
            pool[idx].buffer.release();
            pool.erase(pool.begin() + idx);
        }
    }
}

vk::CommandBuffer TransferManager::beginOneTimeCommands(TransferQueueType queueType)
{
    auto &resources = getThreadResources();
    vk::CommandPool pool =
        (queueType == TransferQueueType::Transfer) ? resources.transferCommandPool : resources.graphicsCommandPool;
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = pool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer cmdBuffer = m_ctx->getDevice().allocateCommandBuffers(allocInfo)[0];
    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    cmdBuffer.begin(beginInfo);
    return cmdBuffer;
}

TransferToken TransferManager::endOneTimeCommands(vk::CommandBuffer cmdBuffer, TransferQueueType queueType,
                                                  std::vector<size_t> stagingBuffersToRelease)
{
    cmdBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer;

    vk::Queue queue;
    if (queueType == TransferQueueType::Transfer && m_ctx->getQueueFamilyIndices().transferFamily.has_value())
        queue = m_ctx->getTransferQueue();
    else
        queue = m_ctx->getGraphicsQueue();

    auto &resources = getThreadResources();
    vk::Device device = m_ctx->getDevice();

    // 检查并清理已完成的提交
    for (auto it = resources.activeSubmissions.begin(); it != resources.activeSubmissions.end();)
    {
        bool signaled = false;
        if (it->tokenState->completed.load(std::memory_order_acquire))
        {
            signaled = true;
        }
        else if (device.getFenceStatus(it->tokenState->fence) == vk::Result::eSuccess)
        {
            it->tokenState->completed.store(true, std::memory_order_release);
            signaled = true;
        }

        // 只清理已经没有外部引用的完成提交
        if (signaled && it->tokenState.use_count() == 1)
        {
            // 释放资源
            device.resetFences(1, &it->tokenState->fence);
            resources.fencePool.push_back(it->tokenState->fence);

            // 释放staging buffers
            for (size_t idx : it->stagingBuffersToRelease)
            {
                releaseStagingBuffer(idx);
            }

            // 释放command buffer
            device.freeCommandBuffers(it->commandPool, it->cmdBuffer);

            it = resources.activeSubmissions.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // 获取可用的fence
    vk::Fence fence;
    if (!resources.fencePool.empty())
    {
        fence = resources.fencePool.back();
        resources.fencePool.pop_back();
    }
    else
    {
        vk::FenceCreateInfo fenceInfo{};
        fence = device.createFence(fenceInfo);
    }

    queue.submit(submitInfo, fence);

    vk::CommandPool pool =
        (queueType == TransferQueueType::Transfer) ? resources.transferCommandPool : resources.graphicsCommandPool;

    // 创建 token state
    auto tokenState = std::make_shared<TransferToken::State>();
    tokenState->fence = fence;
    tokenState->device = device;
    tokenState->completed.store(false, std::memory_order_release);

    // 记录提交
    ThreadResources::PendingSubmission submission{};
    submission.tokenState = tokenState;
    submission.cmdBuffer = cmdBuffer;
    submission.commandPool = pool;
    submission.stagingBuffersToRelease = stagingBuffersToRelease;

    resources.activeSubmissions.push_back(submission);

    return TransferToken{tokenState};
}

ManagedBuffer TransferManager::createStagingBuffer(vk::DeviceSize size)
{
    BufferDesc desc{};
    desc.size = size;
    desc.usage = BufferUsageFlags::StagingSrc | BufferUsageFlags::TransferSrc;
    desc.memory = MemoryUsage::CpuToGpu;
    desc.debugName = "TransferManager_Staging";
    return m_allocator->createBuffer(desc);
}

TransferManager::ThreadResources &TransferManager::getThreadResources()
{
    static thread_local std::unordered_map<TransferManager *, std::weak_ptr<ThreadResources>> tls_resources;

    auto it = tls_resources.find(this);
    if (it != tls_resources.end())
    {
        if (auto ptr = it->second.lock())
        {
            return *ptr;
        }
    }

    auto newResources = std::make_shared<ThreadResources>();

    auto queueFamilies = m_ctx->getQueueFamilyIndices();
    uint32_t graphicsFamily = queueFamilies.graphicsFamily.value();
    uint32_t transferFamily = queueFamilies.transferFamily.value_or(graphicsFamily);

    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

    // Transfer command pool
    poolInfo.queueFamilyIndex = transferFamily;
    newResources->transferCommandPool = m_ctx->getDevice().createCommandPool(poolInfo);

    // Graphics command pool
    poolInfo.queueFamilyIndex = graphicsFamily;
    newResources->graphicsCommandPool = m_ctx->getDevice().createCommandPool(poolInfo);

    {
        std::lock_guard<std::mutex> lock(m_resourcesMutex);
        m_threadResources.push_back(newResources);
    }

    tls_resources[this] = newResources;
    return *newResources;
}

TransferManager::BarrierInfo TransferManager::getBarrierInfo(vk::ImageLayout oldLayout, vk::ImageLayout newLayout) const
{
    BarrierInfo info{};

    auto fill = [](vk::ImageLayout layout, vk::AccessFlags &access, vk::PipelineStageFlags &stage) {
        switch (layout)
        {
        case vk::ImageLayout::eUndefined:
            access = {};
            stage = vk::PipelineStageFlagBits::eTopOfPipe;
            break;
        case vk::ImageLayout::eGeneral:
            access = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
            stage = vk::PipelineStageFlagBits::eComputeShader;
            break;
        case vk::ImageLayout::eColorAttachmentOptimal:
            access = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
            stage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            break;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            access = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            stage = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
            break;
        case vk::ImageLayout::eDepthStencilReadOnlyOptimal:
            access = vk::AccessFlagBits::eDepthStencilAttachmentRead;
            stage = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
            break;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            access = vk::AccessFlagBits::eShaderRead;
            stage = vk::PipelineStageFlagBits::eFragmentShader;
            break;
        case vk::ImageLayout::eTransferSrcOptimal:
            access = vk::AccessFlagBits::eTransferRead;
            stage = vk::PipelineStageFlagBits::eTransfer;
            break;
        case vk::ImageLayout::eTransferDstOptimal:
            access = vk::AccessFlagBits::eTransferWrite;
            stage = vk::PipelineStageFlagBits::eTransfer;
            break;
        case vk::ImageLayout::ePresentSrcKHR:
            access = {};
            stage = vk::PipelineStageFlagBits::eBottomOfPipe;
            break;
        default:
            access = {};
            stage = vk::PipelineStageFlagBits::eTopOfPipe;
            break;
        }
    };

    fill(oldLayout, info.srcAccessMask, info.srcStage);
    fill(newLayout, info.dstAccessMask, info.dstStage);
    return info;
}

void TransferManager::copyHostToStaging(size_t stagingIndex, const void *data, size_t size)
{
    const ManagedBuffer &stagingBuffer = getThreadResources().stagingBufferPool[stagingIndex].buffer;
    void *dst = nullptr;
    if (vmaMapMemory(m_allocator->getAllocator(), stagingBuffer.getAllocation(), &dst))
    {
        throw std::runtime_error("failed to map memory");
    }
    std::memcpy(static_cast<char *>(dst), data, size);
    vmaUnmapMemory(m_allocator->getAllocator(), stagingBuffer.getAllocation());
}