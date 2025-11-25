#include "../public/VkResource.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace vkcore;

// ==================== RAII类实现 ====================

// ManagedBuffer
ManagedBuffer::ManagedBuffer(VkResourceAllocator *owner, vk::Buffer buffer, VmaAllocation allocation,
                             vk::DeviceSize size, const std::string debugName)
    : m_owner(owner), m_buffer(buffer), m_allocation(allocation), m_size(size), m_debugName(debugName)
{
}

ManagedBuffer::ManagedBuffer(ManagedBuffer &&other) noexcept
{
    m_owner = other.m_owner;
    m_buffer = other.m_buffer;
    m_allocation = other.m_allocation;
    m_size = other.m_size;
    m_debugName = std::move(other.m_debugName);

    other.m_owner = nullptr;
    other.m_buffer = VK_NULL_HANDLE;
    other.m_allocation = nullptr;
    other.m_size = 0;
    other.m_debugName.clear();
}

ManagedBuffer &ManagedBuffer::operator=(ManagedBuffer &&other) noexcept
{
    if (this != &other)
    {
        release();

        m_owner = other.m_owner;
        m_buffer = other.m_buffer;
        m_allocation = other.m_allocation;
        m_size = other.m_size;
        m_debugName = std::move(other.m_debugName);

        other.m_owner = nullptr;
        other.m_buffer = VK_NULL_HANDLE;
        other.m_allocation = nullptr;
        other.m_size = 0;
        other.m_debugName.clear();
    }
    return *this;
}

ManagedBuffer::~ManagedBuffer()
{
    release();
}

void ManagedBuffer::release()
{
    if (m_owner && m_buffer)
    {
        m_owner->destroyBuffer(m_buffer, m_allocation);
        m_owner = nullptr;
        m_buffer = nullptr;
        m_allocation = nullptr;
    }
}

ManagedImage::ManagedImage(VkResourceAllocator *owner, vk::ImageView view, vk::Image image, VmaAllocation allocation,
                           vk::Extent3D extent, vk::Format format, vk::ImageAspectFlags aspectMask,
                           const std::string debugName)
    : m_owner(owner), m_view(view), m_image(image), m_allocation(allocation), m_extent(extent), m_format(format),
      m_aspectMask(aspectMask), m_debugName(debugName)
{
}

ManagedImage::ManagedImage(ManagedImage &&other) noexcept
{
    m_owner = other.m_owner;
    m_view = other.m_view;
    m_image = other.m_image;
    m_allocation = other.m_allocation;
    m_extent = other.m_extent;
    m_format = other.m_format;
    m_aspectMask = other.m_aspectMask;
    m_debugName = std::move(other.m_debugName);

    other.m_owner = nullptr;
    other.m_view = VK_NULL_HANDLE;
    other.m_image = VK_NULL_HANDLE;
    other.m_allocation = nullptr;
    other.m_extent = vk::Extent3D{0, 0, 0};
    other.m_format = vk::Format::eUndefined;
    other.m_aspectMask = {};
    other.m_debugName.clear();
}

ManagedImage &ManagedImage::operator=(ManagedImage &&other) noexcept
{
    if (this != &other)
    {
        release();

        m_owner = other.m_owner;
        m_view = other.m_view;
        m_image = other.m_image;
        m_allocation = other.m_allocation;
        m_extent = other.m_extent;
        m_format = other.m_format;
        m_aspectMask = other.m_aspectMask;
        m_debugName = std::move(other.m_debugName);

        other.m_owner = nullptr;
        other.m_view = VK_NULL_HANDLE;
        other.m_image = VK_NULL_HANDLE;
        other.m_allocation = nullptr;
        other.m_extent = vk::Extent3D{0, 0, 0};
        other.m_format = vk::Format::eUndefined;
        other.m_aspectMask = {};
        other.m_debugName.clear();
    }
    return *this;
}

ManagedImage::~ManagedImage()
{
    release();
}

void ManagedImage::release()
{
    if (m_owner)
    {
        if (m_view)
        {
            m_owner->destroyImageView(m_view);
            m_view = nullptr;
        }
        if (m_allocation && m_image)
        {
            m_owner->destroyImage(m_image, m_allocation);
            m_image = nullptr;
            m_allocation = nullptr;
        }
        m_owner = nullptr;
    }
}

// ManagedSampler
ManagedSampler::ManagedSampler(VkResourceAllocator *owner, vk::Sampler sampler, const std::string debugName)
    : m_owner(owner), m_sampler(sampler), m_debugName(debugName)
{
}

ManagedSampler::ManagedSampler(ManagedSampler &&other) noexcept
    : m_owner(other.m_owner), m_sampler(other.m_sampler), m_debugName(other.m_debugName)
{
    other.m_owner = nullptr;
    other.m_sampler = nullptr;
    other.m_debugName = nullptr;
}

ManagedSampler &ManagedSampler::operator=(ManagedSampler &&other) noexcept
{
    if (this != &other)
    {
        release();
        m_owner = other.m_owner;
        m_sampler = other.m_sampler;
        m_debugName = other.m_debugName;

        other.m_owner = nullptr;
        other.m_sampler = nullptr;
        other.m_debugName = nullptr;
    }
    return *this;
}

ManagedSampler::~ManagedSampler()
{
    release();
}

void ManagedSampler::release()
{
    if (m_owner && m_sampler)
    {
        m_owner->destroySampler(m_sampler);
        m_owner = nullptr;
        m_sampler = nullptr;
    }
}

// ==================== VkResourceAllocator实现 ====================

VkResourceAllocator::~VkResourceAllocator()
{
    cleanup();
}

void VkResourceAllocator::initialize(VkContext &ctx)
{
    m_ctx = &ctx;
    if (m_allocator)
        return;

    // 设置VMA的Vulkan函数指针
    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = static_cast<VkPhysicalDevice>(ctx.getPhysicalDevice());
    allocatorInfo.device = static_cast<VkDevice>(ctx.getDevice());
    allocatorInfo.instance = static_cast<VkInstance>(ctx.getVkInstance());
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create allocator");
    }
}

void VkResourceAllocator::cleanup()
{
    if (m_allocator)
    {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }
}

// ==================== 内部转换函数 ====================

vk::BufferUsageFlags VkResourceAllocator::toVkBufferUsage(BufferUsageFlags usage) const
{
    vk::BufferUsageFlags flags;
    uint32_t u = static_cast<uint32_t>(usage);

    if (u & static_cast<uint32_t>(BufferUsageFlags::Vertex))
        flags |= vk::BufferUsageFlagBits::eVertexBuffer;
    if (u & static_cast<uint32_t>(BufferUsageFlags::Index))
        flags |= vk::BufferUsageFlagBits::eIndexBuffer;
    if (u & static_cast<uint32_t>(BufferUsageFlags::Uniform))
        flags |= vk::BufferUsageFlagBits::eUniformBuffer;
    if (u & static_cast<uint32_t>(BufferUsageFlags::Storage))
        flags |= vk::BufferUsageFlagBits::eStorageBuffer;
    if (u & static_cast<uint32_t>(BufferUsageFlags::StagingSrc))
        flags |= vk::BufferUsageFlagBits::eTransferSrc;
    if (u & static_cast<uint32_t>(BufferUsageFlags::StagingDst))
        flags |= vk::BufferUsageFlagBits::eTransferDst;
    if (u & static_cast<uint32_t>(BufferUsageFlags::Indirect))
        flags |= vk::BufferUsageFlagBits::eIndirectBuffer;
    if (u & static_cast<uint32_t>(BufferUsageFlags::TransferSrc))
        flags |= vk::BufferUsageFlagBits::eTransferSrc;
    if (u & static_cast<uint32_t>(BufferUsageFlags::TransferDst))
        flags |= vk::BufferUsageFlagBits::eTransferDst;

    return flags;
}

VmaMemoryUsage VkResourceAllocator::toVmaUsage(MemoryUsage mem) const
{
    switch (mem)
    {
    case MemoryUsage::GpuOnly:
        return VMA_MEMORY_USAGE_GPU_ONLY;
    case MemoryUsage::CpuToGpu:
        return VMA_MEMORY_USAGE_CPU_TO_GPU;
    case MemoryUsage::GpuToCpu:
        return VMA_MEMORY_USAGE_GPU_TO_CPU;
    }
    return VMA_MEMORY_USAGE_UNKNOWN;
}

vk::ImageUsageFlags VkResourceAllocator::toVkImageUsage(ImageUsageFlags usage) const
{
    vk::ImageUsageFlags flags;
    uint32_t u = static_cast<uint32_t>(usage);

    if (u & static_cast<uint32_t>(ImageUsageFlags::ColorRT))
        flags |= vk::ImageUsageFlagBits::eColorAttachment;
    if (u & static_cast<uint32_t>(ImageUsageFlags::DepthStencil))
        flags |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
    if (u & static_cast<uint32_t>(ImageUsageFlags::Sampled))
        flags |= vk::ImageUsageFlagBits::eSampled;
    if (u & static_cast<uint32_t>(ImageUsageFlags::Storage))
        flags |= vk::ImageUsageFlagBits::eStorage;
    if (u & static_cast<uint32_t>(ImageUsageFlags::TransferSrc))
        flags |= vk::ImageUsageFlagBits::eTransferSrc;
    if (u & static_cast<uint32_t>(ImageUsageFlags::TransferDst))
        flags |= vk::ImageUsageFlagBits::eTransferDst;
    if (u & static_cast<uint32_t>(ImageUsageFlags::InputAttachment))
        flags |= vk::ImageUsageFlagBits::eInputAttachment;

    return flags;
}

void VkResourceAllocator::setDebugName(vk::ObjectType objectType, uint64_t objectHandle, std::string name)
{
    if (name.empty() || !m_ctx || !m_ctx->m_pfnSetDebugUtilsObjectName)
        return;

    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = static_cast<VkObjectType>(objectType);
    nameInfo.objectHandle = objectHandle;
    nameInfo.pObjectName = name.c_str();

    m_ctx->m_pfnSetDebugUtilsObjectName(static_cast<VkDevice>(m_ctx->getDevice()), &nameInfo);
}

// ==================== Buffer管理 ====================

ManagedBuffer VkResourceAllocator::createBuffer(const BufferDesc &desc)
{
    if (!m_allocator)
        throw std::runtime_error("Allocator not initialized");

    vk::BufferCreateInfo bufferInfo = {};
    bufferInfo.size = desc.size;
    bufferInfo.usage = toVkBufferUsage(desc.usage);
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = toVmaUsage(desc.memory);

    VkBufferCreateInfo vkBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    VkBuffer vkBuffer;
    VmaAllocation allocation;

    if (vmaCreateBuffer(m_allocator, &vkBufferInfo, &allocInfo, &vkBuffer, &allocation, nullptr) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create buffer");
    }

    vk::Buffer buffer = vkBuffer;

    // 设置Debug名称
    if (!desc.debugName.empty())
    {
        setDebugName(vk::ObjectType::eBuffer, (uint64_t) static_cast<VkBuffer>(buffer), desc.debugName);
    }

    return ManagedBuffer{this, buffer, allocation, desc.size, desc.debugName};
}

void VkResourceAllocator::destroyBuffer(vk::Buffer buffer, VmaAllocation allocation)
{
    if (m_allocator && buffer)
    {
        vmaDestroyBuffer(m_allocator, static_cast<VkBuffer>(buffer), allocation);
    }
}

// ==================== Image管理 ====================

ManagedImage VkResourceAllocator::createImage(const ImageDesc &desc, vk::ImageAspectFlags aspectMask)
{
    if (!m_allocator)
        throw std::runtime_error("Allocator not initialized");

    vk::ImageCreateInfo imgInfo{};
    imgInfo.imageType = desc.type;
    imgInfo.extent = vk::Extent3D{desc.width, desc.height, desc.depth};
    imgInfo.mipLevels = desc.mipLevels;
    imgInfo.arrayLayers = desc.arrayLayers;
    imgInfo.format = desc.format;
    imgInfo.tiling = desc.tiling;
    imgInfo.initialLayout = vk::ImageLayout::eUndefined;
    imgInfo.usage = toVkImageUsage(desc.usage);
    imgInfo.samples = desc.samples;
    imgInfo.sharingMode = vk::SharingMode::eExclusive;
    imgInfo.flags = desc.flags;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = toVmaUsage(desc.memory);

    VkImageCreateInfo vkImgInfo = static_cast<VkImageCreateInfo>(imgInfo);
    VkImage vkImage;
    VmaAllocation allocation;

    if (vmaCreateImage(m_allocator, &vkImgInfo, &allocInfo, &vkImage, &allocation, nullptr) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create image");
    }

    vk::Image image = vkImage;

    // 设置Debug名称
    if (!desc.debugName.empty())
    {
        setDebugName(vk::ObjectType::eImage, (uint64_t) static_cast<VkImage>(image), desc.debugName);
    }

    // Create default ImageView
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = image;
    viewInfo.viewType = (desc.type == vk::ImageType::e2D)   ? vk::ImageViewType::e2D
                        : (desc.type == vk::ImageType::e3D) ? vk::ImageViewType::e3D
                                                            : vk::ImageViewType::e1D;
    // Handle Cube Map
    if (desc.flags & vk::ImageCreateFlagBits::eCubeCompatible)
    {
        viewInfo.viewType = vk::ImageViewType::eCube;
    }

    viewInfo.format = desc.format;
    viewInfo.components = {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
                           vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity};
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc.arrayLayers;

    vk::ImageView view = m_ctx->getDevice().createImageView(viewInfo);

    if (!desc.debugName.empty())
    {
        setDebugName(vk::ObjectType::eImageView, (uint64_t) static_cast<VkImageView>(view), desc.debugName);
    }

    return ManagedImage{this, view, image, allocation, imgInfo.extent, desc.format, aspectMask, desc.debugName};
}

void VkResourceAllocator::destroyImage(vk::Image image, VmaAllocation allocation)
{
    if (m_allocator && image)
    {
        vmaDestroyImage(m_allocator, static_cast<VkImage>(image), allocation);
    }
}
// ==================== ImageView管理 ====================
ManagedImage VkResourceAllocator::createImageView(const ManagedImage &image, vk::ImageAspectFlags aspectMask,
                                                  uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer,
                                                  uint32_t layerCount, vk::ImageViewType viewType,
                                                  const std::string debugName)
{
    if (!m_ctx)
        throw std::runtime_error("Context not initialized");

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = image.getImage();
    viewInfo.viewType = viewType;
    viewInfo.format = image.getFormat();
    viewInfo.components = {vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
                           vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity};
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = baseMipLevel;
    viewInfo.subresourceRange.levelCount = levelCount;
    viewInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
    viewInfo.subresourceRange.layerCount = layerCount;

    vk::ImageView view = m_ctx->getDevice().createImageView(viewInfo);

    if (!debugName.empty())
    {
        setDebugName(vk::ObjectType::eImageView, (uint64_t) static_cast<VkImageView>(view), debugName);
    }

    return ManagedImage{this,       view,     image.getImage(), nullptr, image.getExtent(), image.getFormat(),
                        aspectMask, debugName};
}

void VkResourceAllocator::destroyImageView(vk::ImageView view)
{
    if (m_ctx && view)
    {
        m_ctx->getDevice().destroyImageView(view);
    }
}

// ==================== Sampler管理 ====================

ManagedSampler VkResourceAllocator::createSampler(vk::Filter magFilter, vk::Filter minFilter,
                                                  vk::SamplerMipmapMode mipmapMode, vk::SamplerAddressMode addressMode,
                                                  float maxAnisotropy, const std::string debugName)
{
    if (!m_ctx)
        throw std::runtime_error("Context not initialized");

    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = magFilter;
    samplerInfo.minFilter = minFilter;
    samplerInfo.mipmapMode = mipmapMode;
    samplerInfo.addressModeU = addressMode;
    samplerInfo.addressModeV = addressMode;
    samplerInfo.addressModeW = addressMode;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = (maxAnisotropy > 1.0f) ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = maxAnisotropy;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    vk::Sampler sampler = m_ctx->getDevice().createSampler(samplerInfo);

    if (!debugName.empty())
    {
        setDebugName(vk::ObjectType::eSampler, (uint64_t) static_cast<VkSampler>(sampler), debugName);
    }

    return ManagedSampler{this, sampler, debugName};
}

void VkResourceAllocator::destroySampler(vk::Sampler sampler)
{
    if (m_ctx && sampler)
    {
        m_ctx->getDevice().destroySampler(sampler);
    }
}