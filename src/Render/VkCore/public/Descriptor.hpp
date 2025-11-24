// Descriptor.hpp
#pragma once

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vkcore
{

// 前向声明资源类型（在 VkResource.hpp 中定义）
class ManagedBuffer;
class ManagedImage;
class ManagedSampler;

// =====================================================================================
// 反射出来的单个 binding 信息（语义级描述）
// 注意：这里不再保存 setIndex，setIndex 由 DescriptorSetSchema 管理
// =====================================================================================
struct DescriptorBindingInfo
{
    std::string name;     // 例如 "albedo", "camera", "lights"
    uint32_t binding = 0; // Vulkan binding index
    vk::DescriptorType descriptorType{};
    uint32_t descriptorCount = 1;
    vk::ShaderStageFlags stageFlags{}; // 该 binding 被哪些 shader stage 使用
};

// =====================================================================================
// 单个 DescriptorSet Layout 的“Schema”描述（一个 descriptor set 对应一个 schema）
// =====================================================================================
class DescriptorSetSchema
{
  public:
    DescriptorSetSchema() = default;

    const std::string &getName() const noexcept
    {
        return m_name;
    }
    uint32_t getSetIndex() const noexcept
    {
        return m_setIndex;
    }
    vk::DescriptorSetLayout getLayout() const noexcept
    {
        return m_layout;
    }

    const std::vector<DescriptorBindingInfo> &getBindings() const noexcept
    {
        return m_bindings;
    }

    /// 按语义名查找 binding 信息（找不到返回 nullptr）
    const DescriptorBindingInfo *findBinding(std::string_view name) const noexcept;

  private:
    friend class DescriptorSetLayoutCache;

    std::string m_name; // schema 名，例如 "PerMaterial", "PerFrame"
    uint32_t m_setIndex = 0;
    vk::DescriptorSetLayout m_layout{};
    // 该 set 上所有 binding（保证按 binding index 升序排序）
    std::vector<DescriptorBindingInfo> m_bindings;
};

// =====================================================================================
// Layout 缓存：反射结果注册进来后，全局长期存在，负责：
//  - 根据“结构 + setIndex”创建/复用 vk::DescriptorSetLayout
//  - 通过 (schemaName, setIndex) 提供 DescriptorSetSchema
// =====================================================================================
class DescriptorSetLayoutCache
{
  public:
    explicit DescriptorSetLayoutCache(vk::Device device);
    ~DescriptorSetLayoutCache();

    DescriptorSetLayoutCache(const DescriptorSetLayoutCache &) = delete;
    DescriptorSetLayoutCache &operator=(const DescriptorSetLayoutCache &) = delete;

    /// 注册一个布局（通常由 Shader 反射系统调用）
    /// 同一 schemaName + setIndex 多次注册时：
    ///  - 如果绑定结构一致：复用已有 Schema
    ///  - 如果绑定结构不一致：抛出 std::runtime_error（防止 API 被误用）
    std::shared_ptr<DescriptorSetSchema> registerSetLayout(const std::string &schemaName, uint32_t setIndex,
                                                           const std::vector<DescriptorBindingInfo> &bindings);

    /// 获取 Schema（若未注册则返回 nullptr）
    std::shared_ptr<const DescriptorSetSchema> getSchema(const std::string &schemaName, uint32_t setIndex = 0) const;

    /// 便捷函数：直接返回 layout（未注册时返回空 handle）
    vk::DescriptorSetLayout getLayout(const std::string &schemaName, uint32_t setIndex = 0) const;

    /// 释放所有缓存的布局（一般在程序退出时调用）
    void cleanup();

  private:
    // 内部用于唯一标识 layout 结构的 key（setIndex + bindings 结构 hash）
    struct LayoutKey
    {
        uint32_t setIndex = 0;
        // 只参与结构比较的字段：binding / descriptorType / descriptorCount / stageFlags
        // name 不参与 hash / equal，但会保留在 Schema 中用于语义查找
        std::vector<DescriptorBindingInfo> bindings;
    };

    struct LayoutKeyHash
    {
        std::size_t operator()(const LayoutKey &key) const noexcept;
    };

    struct LayoutKeyEqual
    {
        bool operator()(const LayoutKey &a, const LayoutKey &b) const noexcept;
    };

    static std::string makeNameKey(const std::string &schemaName, uint32_t setIndex);

  private:
    vk::Device m_device{};

    mutable std::mutex m_mutex;

    // 结构 -> Schema
    std::unordered_map<LayoutKey, std::shared_ptr<DescriptorSetSchema>, LayoutKeyHash, LayoutKeyEqual> m_schemasByKey;

    // (schemaName + setIndex) -> Schema（弱引用避免循环）
    std::unordered_map<std::string, std::weak_ptr<DescriptorSetSchema>> m_schemasByName;
};

// =====================================================================================
// 描述符池管理：按 Schema 分配/重用 DescriptorSet
// - 不再直接用 Layout 作为外部接口，而是用 DescriptorSetSchema
// =====================================================================================
class DescriptorPoolAllocator
{
  public:
    DescriptorPoolAllocator(vk::Device device, DescriptorSetLayoutCache &layoutCache);
    ~DescriptorPoolAllocator();

    DescriptorPoolAllocator(const DescriptorPoolAllocator &) = delete;
    DescriptorPoolAllocator &operator=(const DescriptorPoolAllocator &) = delete;

    /// 为指定 Schema 分配一个 DescriptorSet
    vk::DescriptorSet allocate(const std::shared_ptr<const DescriptorSetSchema> &schema);

    /// 批量分配 DescriptorSet
    std::vector<vk::DescriptorSet> allocate(const std::shared_ptr<const DescriptorSetSchema> &schema, uint32_t count);

    /// 重置所有已用的池为“空闲”状态（不销毁）
    void resetPools();

    /// 销毁所有池
    void cleanup();

  private:
    vk::DescriptorPool acquirePool();

  private:
    vk::Device m_device{};
    DescriptorSetLayoutCache *m_layoutCache = nullptr;

    vk::DescriptorPool m_currentPool{};
    std::vector<vk::DescriptorPool> m_usedPools;
    std::vector<vk::DescriptorPool> m_freePools;

    mutable std::mutex m_mutex;
};

// =====================================================================================
// 描述符写入器：按“语义名”写入资源，而不是绑定号
//
// 行为约定：
//  - 若 bindingName 在 Schema 中不存在：抛出 std::runtime_error
//  - 同一 binding 多次写入：后写覆盖前写
//  - 数组写入：实际写入 min(N, binding.descriptorCount) 个，
//              当 N > descriptorCount 时，使用“最后的 descriptorCount 个”
//              （优先后面的元素）
//
// 用法示意：
//   auto schema = layoutCache.getSchema("PerMaterial", 0);
//   auto set    = allocator.allocate(schema);
//   DescriptorSetWriter::begin(device, schema, set)
//       .writeImage("albedo", albedoInfo)
//       .writeBuffer("materialData", materialBufferInfo)
//       .update();
// =====================================================================================
class DescriptorSetWriter
{
  public:
    /// 创建一个 writer，基于指定的 schema + descriptor set
    static DescriptorSetWriter begin(vk::Device device, std::shared_ptr<const DescriptorSetSchema> schema,
                                     vk::DescriptorSet set);

    /// 写入单个缓冲区（按 binding 名字，而不是 binding index）
    DescriptorSetWriter &writeBuffer(std::string_view bindingName, const vk::DescriptorBufferInfo &bufferInfo);

    /// 写入多个缓冲区（数组形式）
    DescriptorSetWriter &writeBuffers(std::string_view bindingName,
                                      const std::vector<vk::DescriptorBufferInfo> &bufferInfos);

    /// 写入单个图像（采样纹理 / storage image 等）
    DescriptorSetWriter &writeImage(std::string_view bindingName, const vk::DescriptorImageInfo &imageInfo);

    /// 写入多个图像（数组形式）
    DescriptorSetWriter &writeImages(std::string_view bindingName,
                                     const std::vector<vk::DescriptorImageInfo> &imageInfos);

    /// 便捷：使用 ManagedBuffer
    DescriptorSetWriter &writeBuffer(std::string_view bindingName, const ManagedBuffer &buffer,
                                     vk::DeviceSize offset = 0);

    /// 便捷：使用 ManagedImage + vk::Sampler（采样纹理）
    DescriptorSetWriter &writeImage(std::string_view bindingName, const ManagedImage &image, vk::Sampler sampler,
                                    vk::ImageLayout layout);

    /// 便捷：使用 ManagedImage + ManagedSampler（采样纹理）
    DescriptorSetWriter &writeSampledImage(std::string_view bindingName, const ManagedImage &image,
                                           const ManagedSampler &sampler, vk::ImageLayout layout);

    /// 便捷：写入 storage image（不使用 sampler）
    DescriptorSetWriter &writeStorageImage(std::string_view bindingName, const ManagedImage &image,
                                           vk::ImageLayout layout);

    /// 提交所有写入（一次性调用 vk::Device::updateDescriptorSets）
    /// 调用后，内部缓存会被清空，可以继续复用该 writer 做下一轮写入。
    void update();

  private:
    DescriptorSetWriter(vk::Device device, std::shared_ptr<const DescriptorSetSchema> schema, vk::DescriptorSet set);

    /// 内部查找 binding 信息（若找不到则抛 std::runtime_error）
    const DescriptorBindingInfo *findBindingOrThrow(std::string_view bindingName) const;

    struct BufferWrite
    {
        uint32_t binding = 0;
        vk::DescriptorType type{};
        uint32_t maxCount = 1; // 来自 DescriptorBindingInfo::descriptorCount
        std::vector<vk::DescriptorBufferInfo> infos;
    };

    struct ImageWrite
    {
        uint32_t binding = 0;
        vk::DescriptorType type{};
        uint32_t maxCount = 1;
        std::vector<vk::DescriptorImageInfo> infos;
    };

    BufferWrite *findOrCreateBufferWrite(uint32_t binding, vk::DescriptorType type, uint32_t maxCount);
    ImageWrite *findOrCreateImageWrite(uint32_t binding, vk::DescriptorType type, uint32_t maxCount);

  private:
    vk::Device m_device{};
    std::shared_ptr<const DescriptorSetSchema> m_schema;
    vk::DescriptorSet m_set{};

    std::vector<BufferWrite> m_bufferWrites;
    std::vector<ImageWrite> m_imageWrites;
};

} // namespace vkcore
