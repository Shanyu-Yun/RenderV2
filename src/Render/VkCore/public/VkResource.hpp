#pragma once

#include "VkContext.hpp"
#include <cstdint>
#include <memory>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace vkcore
{

// ==================== 枚举定义 ====================

/**
 * @brief Buffer使用标志（语义化）
 */
enum class BufferUsageFlags : uint32_t
{
    None = 0,
    Vertex = 1 << 0,      ///< 顶点缓冲
    Index = 1 << 1,       ///< 索引缓冲
    Uniform = 1 << 2,     ///< Uniform缓冲
    Storage = 1 << 3,     ///< Storage缓冲
    StagingSrc = 1 << 4,  ///< 暂存缓冲（Host→Device）
    StagingDst = 1 << 5,  ///< 回读缓冲（Device→Host）
    Indirect = 1 << 6,    ///< 间接绘制缓冲
    TransferSrc = 1 << 7, ///< 传输源
    TransferDst = 1 << 8, ///< 传输目标
};

inline BufferUsageFlags operator|(BufferUsageFlags a, BufferUsageFlags b)
{
    return static_cast<BufferUsageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline BufferUsageFlags operator&(BufferUsageFlags a, BufferUsageFlags b)
{
    return static_cast<BufferUsageFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool operator!(BufferUsageFlags a)
{
    return static_cast<uint32_t>(a) == 0;
}

/**
 * @brief 内存使用模式
 */
enum class MemoryUsage
{
    GpuOnly,  ///< 仅GPU访问（最快，不能map）
    CpuToGpu, ///< CPU写入，GPU读取（上传）
    GpuToCpu  ///< GPU写入，CPU读取（回读）
};

/**
 * @brief Image使用标志（语义化）
 */
enum class ImageUsageFlags : uint32_t
{
    None = 0,
    ColorRT = 1 << 0,        ///< 颜色渲染目标
    DepthStencil = 1 << 1,   ///< 深度模板
    Sampled = 1 << 2,        ///< 采样纹理
    Storage = 1 << 3,        ///< Storage Image
    TransferSrc = 1 << 4,    ///< 传输源
    TransferDst = 1 << 5,    ///< 传输目标
    InputAttachment = 1 << 6 ///< 输入附件
};

inline ImageUsageFlags operator|(ImageUsageFlags a, ImageUsageFlags b)
{
    return static_cast<ImageUsageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ImageUsageFlags operator&(ImageUsageFlags a, ImageUsageFlags b)
{
    return static_cast<ImageUsageFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

// ==================== 描述结构体 ====================

/**
 * @brief Buffer创建描述
 */
struct BufferDesc
{
    vk::DeviceSize size = 0;
    BufferUsageFlags usage = BufferUsageFlags::None;
    MemoryUsage memory = MemoryUsage::GpuOnly;
    const char *debugName = nullptr;
};

/**
 * @brief Image创建描述
 */
struct ImageDesc
{
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    vk::Format format = vk::Format::eUndefined;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    ImageUsageFlags usage = static_cast<ImageUsageFlags>(static_cast<uint32_t>(ImageUsageFlags::ColorRT) |
                                                         static_cast<uint32_t>(ImageUsageFlags::Sampled));
    MemoryUsage memory = MemoryUsage::GpuOnly;
    vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
    vk::ImageType type = vk::ImageType::e2D;
    vk::ImageCreateFlags flags = {};
    const char *debugName = nullptr;
};

// ==================== RAII封装 ====================

class VkResourceAllocator; // 前向声明

/**
 * @brief RAII管理的Buffer
 * 析构时自动释放 VMA allocation
 */
class ManagedBuffer
{
  public:
    ManagedBuffer() = default;
    ManagedBuffer(VkResourceAllocator *owner, vk::Buffer buffer, VmaAllocation allocation, vk::DeviceSize size,
                  const char *debugName = nullptr);
    ManagedBuffer(const ManagedBuffer &) = delete;
    ManagedBuffer &operator=(const ManagedBuffer &) = delete;
    ManagedBuffer(ManagedBuffer &&other) noexcept;
    ManagedBuffer &operator=(ManagedBuffer &&other) noexcept;
    ~ManagedBuffer();

    vk::Buffer getBuffer() const
    {
        return m_buffer;
    }
    VmaAllocation getAllocation() const
    {
        return m_allocation;
    }
    vk::DeviceSize getSize() const
    {
        return m_size;
    }
    const char *getDebugName() const
    {
        return m_debugName;
    }

    operator bool() const
    {
        return m_buffer;
    }

    void release();

  private:
    VkResourceAllocator *m_owner = nullptr;
    vk::Buffer m_buffer{};
    VmaAllocation m_allocation = nullptr;
    vk::DeviceSize m_size = 0;
    const char *m_debugName = nullptr;
};

/**
 * @brief RAII管理的Image
 * 包含 vk::Image, VmaAllocation 和默认的 vk::ImageView
 * 析构时自动释放 View，如果持有 Allocation 则同时释放 Image
 */
class ManagedImage
{
  public:
    ManagedImage() = default;

    ManagedImage(VkResourceAllocator *owner, vk::ImageView view, vk::Image image, VmaAllocation allocation,
                 vk::Extent3D extent, vk::Format format, vk::ImageAspectFlags aspectMask,
                 const char *debugName = nullptr);

    ManagedImage(const ManagedImage &) = delete;
    ManagedImage &operator=(const ManagedImage &) = delete;
    ManagedImage(ManagedImage &&other) noexcept;
    ManagedImage &operator=(ManagedImage &&other) noexcept;
    ~ManagedImage();

    vk::ImageView getView() const
    {
        return m_view;
    }
    vk::Image getImage() const
    {
        return m_image;
    }
    VmaAllocation getAllocation() const
    {
        return m_allocation;
    }
    vk::Extent3D getExtent() const
    {
        return m_extent;
    }
    vk::Format getFormat() const
    {
        return m_format;
    }
    vk::ImageAspectFlags getAspectMask() const
    {
        return m_aspectMask;
    }
    const char *getDebugName() const
    {
        return m_debugName;
    }

    operator bool() const
    {
        return m_image && m_view;
    }

    void release();

  private:
    VkResourceAllocator *m_owner = nullptr;
    vk::ImageView m_view{};
    vk::Image m_image{};
    VmaAllocation m_allocation = nullptr;
    vk::Extent3D m_extent{0, 0, 0};
    vk::Format m_format = vk::Format::eUndefined;
    vk::ImageAspectFlags m_aspectMask{};
    const char *m_debugName = nullptr;
};

/**
 * @brief RAII管理的Sampler
 * 析构时自动销毁 vk::Sampler
 */
class ManagedSampler
{
  public:
    ManagedSampler() = default;
    ManagedSampler(VkResourceAllocator *owner, vk::Sampler sampler, const char *debugName = nullptr);
    ManagedSampler(const ManagedSampler &) = delete;
    ManagedSampler &operator=(const ManagedSampler &) = delete;
    ManagedSampler(ManagedSampler &&other) noexcept;
    ManagedSampler &operator=(ManagedSampler &&other) noexcept;
    ~ManagedSampler();

    vk::Sampler getSampler() const
    {
        return m_sampler;
    }
    const char *getDebugName() const
    {
        return m_debugName;
    }

    operator bool() const
    {
        return m_sampler;
    }

    void release();

  private:
    VkResourceAllocator *m_owner = nullptr;
    vk::Sampler m_sampler{};
    const char *m_debugName = nullptr;
};

// ==================== VkResourceAllocator主类 ====================

class VkResourceAllocator
{
    // 友元声明：允许RAII类访问私有销毁方法
    friend class ManagedBuffer;
    friend class ManagedImage;
    friend class ManagedSampler;

  public:
    VkResourceAllocator() = default;
    ~VkResourceAllocator();

    /** 初始化，需在 VkContext 初始化之后调用 */
    void initialize(VkContext &ctx);
    void cleanup();

    // ==================== Buffer管理 ====================

    /** 创建Buffer（返回RAII管理对象） */
    ManagedBuffer createBuffer(const BufferDesc &desc);

    // ==================== Image管理 ====================

    /** 创建Image并创建默认ImageView（返回RAII管理对象） */
    ManagedImage createImage(const ImageDesc &desc, vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor);

    // ==================== ImageView管理 ====================

    /** 从已有的ManagedImage创建新的ImageView（例如创建不同MipLevel的View） */
    ManagedImage createImageView(const ManagedImage &image, vk::ImageAspectFlags aspectMask, uint32_t baseMipLevel = 0,
                                 uint32_t levelCount = 1, uint32_t baseArrayLayer = 0, uint32_t layerCount = 1,
                                 vk::ImageViewType viewType = vk::ImageViewType::e2D, const char *debugName = nullptr);

    // ==================== Sampler管理 ====================

    /** 创建Sampler */
    ManagedSampler createSampler(vk::Filter magFilter = vk::Filter::eLinear, vk::Filter minFilter = vk::Filter::eLinear,
                                 vk::SamplerMipmapMode mipmapMode = vk::SamplerMipmapMode::eLinear,
                                 vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat,
                                 float maxAnisotropy = 1.0f, const char *debugName = nullptr);

    // ==================== Getter ====================

    VmaAllocator getAllocator() const
    {
        return m_allocator;
    }
    VkContext *getContext() const
    {
        return m_ctx;
    }

  private:
    // 内部转换函数
    vk::BufferUsageFlags toVkBufferUsage(BufferUsageFlags usage) const;
    VmaMemoryUsage toVmaUsage(MemoryUsage mem) const;
    vk::ImageUsageFlags toVkImageUsage(ImageUsageFlags usage) const;

    // 设置Debug名称
    void setDebugName(vk::ObjectType objectType, uint64_t objectHandle, const char *name);

    // 内部资源销毁方法（仅供RAII类调用）
    void destroyBuffer(vk::Buffer buffer, VmaAllocation allocation);
    void destroyImage(vk::Image image, VmaAllocation allocation);
    void destroyImageView(vk::ImageView view);
    void destroySampler(vk::Sampler sampler);

    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkContext *m_ctx = nullptr;
};

} // namespace vkcore