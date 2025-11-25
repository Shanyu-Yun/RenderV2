/**
 * @file TransferManager.hpp
 * @author Summer
 * @brief Vulkan传输管理器，负责数据上传、命令缓冲管理和staging buffer池化
 *
 * 该文件提供了高效的数据传输管理，包括：
 * - 单次提交命令缓冲（one-time submit）
 * - Staging buffer池化管理
 * - 支持Transfer Queue和Graphics Queue
 * - Mipmap生成支持
 * - Buffer到Buffer传输
 * - Buffer到Image传输
 *
 * @version 1.0
 * @date 2025-11-22
 */

#pragma once

#include "VkContext.hpp"
#include "VkResource.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace vkcore
{

/**
 * @brief 传输队列类型
 */
enum class TransferQueueType
{
    Transfer, ///< 专用传输队列（如果可用）
    Graphics  ///< 图形队列（用于mipmap生成等需要图形功能的操作）
};

/**
 * @brief 传输令牌，用于异步等待
 */
struct TransferToken
{
    struct State
    {
        vk::Fence fence;
        vk::Device device;
        std::atomic<bool> completed{false};
    };
    std::shared_ptr<State> state;

    /**
     * @brief 等待传输完成
     * @param timeout 超时时间（纳秒）
     */
    void wait(uint64_t timeout = UINT64_MAX) const
    {
        if (!state || state->completed.load(std::memory_order_acquire))
            return;

        if (state->fence && state->device)
        {
            if (state->device.waitForFences(1, &state->fence, VK_TRUE, timeout) != vk::Result::eSuccess)
            {
                throw std::runtime_error("Wait for fence failed");
            }
            state->completed.store(true, std::memory_order_release);
        }
    }

    /**
     * @brief 检查传输是否完成
     * @return true 如果完成或无效，false 如果仍在进行
     */
    bool isComplete() const
    {
        if (!state)
            return true;
        if (state->completed.load(std::memory_order_acquire))
            return true;

        if (state->fence && state->device)
        {
            if (state->device.getFenceStatus(state->fence) == vk::Result::eSuccess)
            {
                state->completed.store(true, std::memory_order_release);
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief Staging Buffer信息
 */
struct StagingBufferInfo
{
    ManagedBuffer buffer;
    vk::DeviceSize size = 0;
    bool inUse = false;
};

/**
 * @brief 传输管理器配置
 */
struct TransferManagerConfig
{
    bool enableStagingBufferPool = true;                    ///< 是否启用staging buffer池化
    size_t maxPooledStagingBuffers = 8;                     ///< 池中最大staging buffer数量
    vk::DeviceSize minStagingBufferSize = 1024 * 1024;      ///< 最小staging buffer大小（1MB）
    vk::DeviceSize maxStagingBufferSize = 64 * 1024 * 1024; ///< 最大staging buffer大小（64MB）
};

/**
 * @brief Vulkan传输管理器
 *
 * 负责管理数据传输操作，包括：
 * - 维护Transfer和Graphics命令池
 * - 管理staging buffer池
 * - 支持mipmap生成
 */
class TransferManager
{
  public:
    TransferManager() = default;
    ~TransferManager();

    TransferManager(const TransferManager &) = delete;
    TransferManager &operator=(const TransferManager &) = delete;

    /**
     * @brief 初始化传输管理器
     * @param ctx Vulkan上下文引用
     * @param allocator 资源分配器引用
     * @param config 配置参数
     */
    void initialize(VkContext &ctx, VkResourceAllocator &allocator, const TransferManagerConfig &config = {});

    /**
     * @brief 清理传输管理器
     */
    void cleanup();

    // ==================== Buffer传输 ====================

    /**
     * @brief 上传数据到Buffer（自动使用staging buffer）
     * @param dstBuffer 目标buffer
     * @param data 数据指针
     * @param size 数据大小
     * @param dstOffset 目标buffer偏移
     * @return TransferToken 传输令牌
     */
    TransferToken uploadToBuffer(const ManagedBuffer &dstBuffer, const void *data, vk::DeviceSize size,
                                 vk::DeviceSize dstOffset = 0);

    /**
     * @brief 上传数据到Buffer（模板版本）
     */
    template <typename T>
    TransferToken uploadToBuffer(const ManagedBuffer &dstBuffer, const T &obj, vk::DeviceSize dstOffset = 0)
    {
        return uploadToBuffer(dstBuffer, &obj, sizeof(T), dstOffset);
    }

    /**
     * @brief 上传数组数据到Buffer
     */
    template <typename T>
    TransferToken uploadToBuffer(const ManagedBuffer &dstBuffer, const std::vector<T> &data,
                                 vk::DeviceSize dstOffset = 0)
    {
        return uploadToBuffer(dstBuffer, data.data(), data.size() * sizeof(T), dstOffset);
    }

    /**
     * @brief Buffer到Buffer复制
     * @param srcBuffer 源buffer
     * @param dstBuffer 目标buffer
     * @param size 复制大小
     * @param srcOffset 源偏移
     * @param dstOffset 目标偏移
     * @return TransferToken 传输令牌
     */
    TransferToken copyBuffer(const ManagedBuffer &srcBuffer, const ManagedBuffer &dstBuffer, vk::DeviceSize size,
                             vk::DeviceSize srcOffset = 0, vk::DeviceSize dstOffset = 0);
    /**
     * @brief 写入数据到Uniform Buffer
     *
     * @param dstBuffer 目标Uniform Buffer
     * @param data 数据指针
     * @param size 数据大小
     * @param dstOffset 目标偏移
     * @return void
     */
    void writeToUniformBuffer(const ManagedBuffer &dstBuffer, const void *data, vk::DeviceSize size,
                              vk::DeviceSize dstOffset = 0);
    // ==================== Image传输 ====================

    /**
     * @brief 上传数据到Image（自动使用staging buffer）
     * @param dstImage 目标image
     * @param data 数据指针
     * @param dataSize 数据大小
     * @param width 图像宽度
     * @param height 图像高度
     * @param depth 图像深度
     * @param mipLevel 目标mip level
     * @param arrayLayer 目标数组层
     * @return TransferToken 传输令牌
     */
    TransferToken uploadToImage(const ManagedImage &dstImage, const void *data, vk::DeviceSize dataSize, uint32_t width,
                                uint32_t height, uint32_t depth = 1, uint32_t mipLevel = 0, uint32_t arrayLayer = 0);

    /**
     * @brief Buffer到Image复制
     * @param srcBuffer 源buffer
     * @param dstImage 目标image
     * @param width 图像宽度
     * @param height 图像高度
     * @param depth 图像深度
     * @param mipLevel 目标mip level
     * @param arrayLayer 目标数组层
     * @return TransferToken 传输令牌
     */
    TransferToken copyBufferToImage(const ManagedBuffer &srcBuffer, const ManagedImage &dstImage, uint32_t width,
                                    uint32_t height, uint32_t depth = 1, uint32_t mipLevel = 0,
                                    uint32_t arrayLayer = 0);

    /**
     * @brief Image布局转换
     * @param image 目标image
     * @param oldLayout 旧布局
     * @param newLayout 新布局
     * @param aspectMask 图像方面标志
     * @param baseMipLevel 基础mip level
     * @param levelCount mip level数量
     * @param baseArrayLayer 基础数组层
     * @param layerCount 数组层数量
     * @param useGraphicsQueue 是否使用图形队列
     * @return TransferToken 传输令牌
     */
    TransferToken transitionImageLayout(const ManagedImage &image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
                                        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
                                        uint32_t baseMipLevel = 0, uint32_t levelCount = VK_REMAINING_MIP_LEVELS,
                                        uint32_t baseArrayLayer = 0, uint32_t layerCount = VK_REMAINING_ARRAY_LAYERS,
                                        bool useGraphicsQueue = false);

    /**
     * @brief 生成Mipmap（使用blit，需要图形队列）
     * @param image 目标image
     * @param width 图像宽度
     * @param height 图像高度
     * @param mipLevels mip level数量
     * @return TransferToken 传输令牌
     */
    TransferToken generateMipmaps(const ManagedImage &image, uint32_t width, uint32_t height, uint32_t mipLevels);

    // ==================== Staging Buffer管理 ====================

    /**
     * @brief 从池中获取或创建staging buffer
     * @param size 所需大小
     * @return staging buffer索引
     */
    size_t acquireStagingBuffer(vk::DeviceSize size);

    /**
     * @brief 释放staging buffer回池
     * @param index staging buffer索引
     */
    void releaseStagingBuffer(size_t index);

    /**
     * @brief 清理未使用的staging buffer
     */
    void cleanupUnusedStagingBuffers();

    // ==================== Getter ====================

    VkContext *getContext() const
    {
        return m_ctx;
    }
    VkResourceAllocator *getAllocator() const
    {
        return m_allocator;
    }
    const TransferManagerConfig &getConfig() const
    {
        return m_config;
    }

  private:
    struct ThreadResources
    {
        vk::CommandPool transferCommandPool;
        vk::CommandPool graphicsCommandPool;
        std::vector<StagingBufferInfo> stagingBufferPool;

        // 异步提交管理
        struct PendingSubmission
        {
            std::shared_ptr<TransferToken::State> tokenState;
            vk::CommandBuffer cmdBuffer;
            vk::CommandPool commandPool;
            std::vector<size_t> stagingBuffersToRelease;
        };
        std::vector<PendingSubmission> activeSubmissions;
        std::vector<vk::Fence> fencePool;
    };

    void copyHostToStaging(size_t stagingIndex, const void *data, size_t size);

    // 内部辅助函数
    vk::CommandBuffer beginOneTimeCommands(TransferQueueType queueType);
    TransferToken endOneTimeCommands(vk::CommandBuffer cmdBuffer, TransferQueueType queueType,
                                     std::vector<size_t> stagingBuffersToRelease = {});

    ManagedBuffer createStagingBuffer(vk::DeviceSize size);
    ThreadResources &getThreadResources();

    // 获取访问掩码和管线阶段
    struct BarrierInfo
    {
        vk::AccessFlags srcAccessMask;
        vk::AccessFlags dstAccessMask;
        vk::PipelineStageFlags srcStage;
        vk::PipelineStageFlags dstStage;
    };
    BarrierInfo getBarrierInfo(vk::ImageLayout oldLayout, vk::ImageLayout newLayout) const;

    VkContext *m_ctx = nullptr;
    VkResourceAllocator *m_allocator = nullptr;
    TransferManagerConfig m_config;

    // 线程局部资源管理
    std::vector<std::shared_ptr<ThreadResources>> m_threadResources;
    std::mutex m_resourcesMutex;
};

} // namespace vkcore
