// Descriptor.cpp
#include "Descriptor.hpp"
#include "VkResource.hpp"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace vkcore
{

// =====================================================================================
// 内部工具：hash_combine
// =====================================================================================
template <class T> inline void hash_combine(std::size_t &seed, const T &v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

// =====================================================================================
// DescriptorSetSchema
// =====================================================================================

const DescriptorBindingInfo *DescriptorSetSchema::findBinding(std::string_view name) const noexcept
{
    for (const auto &binding : m_bindings)
    {
        if (binding.name == name)
        {
            return &binding;
        }
    }
    return nullptr;
}

// =====================================================================================
// DescriptorSetLayoutCache
// =====================================================================================

DescriptorSetLayoutCache::DescriptorSetLayoutCache(vk::Device device) : m_device(device)
{
}

DescriptorSetLayoutCache::~DescriptorSetLayoutCache()
{
    cleanup();
}

std::string DescriptorSetLayoutCache::makeNameKey(const std::string &schemaName, uint32_t setIndex)
{
    return schemaName + "#" + std::to_string(setIndex);
}

std::size_t DescriptorSetLayoutCache::LayoutKeyHash::operator()(const LayoutKey &key) const noexcept
{
    std::size_t seed = 0;
    hash_combine(seed, key.setIndex);
    for (const auto &b : key.bindings)
    {
        hash_combine(seed, b.binding);
        hash_combine(seed, static_cast<uint32_t>(b.descriptorType));
        hash_combine(seed, b.descriptorCount);
        hash_combine(seed, static_cast<uint32_t>(b.stageFlags));
    }
    return seed;
}

bool DescriptorSetLayoutCache::LayoutKeyEqual::operator()(const LayoutKey &a, const LayoutKey &b) const noexcept
{
    if (a.setIndex != b.setIndex)
        return false;
    if (a.bindings.size() != b.bindings.size())
        return false;

    for (size_t i = 0; i < a.bindings.size(); ++i)
    {
        const auto &ba = a.bindings[i];
        const auto &bb = b.bindings[i];

        if (ba.binding != bb.binding)
            return false;
        if (ba.descriptorType != bb.descriptorType)
            return false;
        if (ba.descriptorCount != bb.descriptorCount)
            return false;
        if (ba.stageFlags != bb.stageFlags)
            return false;
    }
    return true;
}

std::shared_ptr<DescriptorSetSchema> DescriptorSetLayoutCache::registerSetLayout(
    const std::string &schemaName, uint32_t setIndex, const std::vector<DescriptorBindingInfo> &bindings)
{
    // 先做一份规范化拷贝：按 binding index 排序
    std::vector<DescriptorBindingInfo> canonicalBindings = bindings;
    std::sort(canonicalBindings.begin(), canonicalBindings.end(),
              [](const DescriptorBindingInfo &a, const DescriptorBindingInfo &b) { return a.binding < b.binding; });

    std::lock_guard<std::mutex> lock(m_mutex);

    const std::string nameKey = makeNameKey(schemaName, setIndex);

    // 1. 若该 name + setIndex 已注册过，则检查结构是否一致
    if (auto itName = m_schemasByName.find(nameKey); itName != m_schemasByName.end())
    {
        if (auto existing = itName->second.lock())
        {
            // 比较 existing->m_bindings 与 canonicalBindings 的结构（忽略 name）
            if (existing->m_bindings.size() != canonicalBindings.size())
            {
                throw std::runtime_error("DescriptorSetLayoutCache::registerSetLayout: schema already registered "
                                         "with different bindings (size mismatch)");
            }

            for (size_t i = 0; i < canonicalBindings.size(); ++i)
            {
                const auto &a = existing->m_bindings[i];
                const auto &b = canonicalBindings[i];
                if (a.binding != b.binding || a.descriptorType != b.descriptorType ||
                    a.descriptorCount != b.descriptorCount || a.stageFlags != b.stageFlags)
                {
                    throw std::runtime_error("DescriptorSetLayoutCache::registerSetLayout: schema already registered "
                                             "with different bindings (structure mismatch)");
                }
            }

            // 结构一致，直接复用原来的 Schema
            return existing;
        }
    }

    // 2. 构建 LayoutKey，按结构查找是否已经有一样的 Layout
    LayoutKey key;
    key.setIndex = setIndex;
    key.bindings = canonicalBindings; // 这里复制一份，作为 key 的内部数据

    auto itKey = m_schemasByKey.find(key);
    if (itKey != m_schemasByKey.end())
    {
        // 同结构但不同 schemaName：复用同一个 Schema
        auto schema = itKey->second;
        m_schemasByName[nameKey] = schema;
        return schema;
    }

    // 3. 不存在：创建 vk::DescriptorSetLayout
    std::vector<vk::DescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(canonicalBindings.size());
    for (const auto &b : canonicalBindings)
    {
        vk::DescriptorSetLayoutBinding vb{};
        vb.binding = b.binding;
        vb.descriptorType = b.descriptorType;
        vb.descriptorCount = b.descriptorCount;
        vb.stageFlags = b.stageFlags;
        vb.pImmutableSamplers = nullptr; // 如需要 immutable samplers，可在外部扩展
        vkBindings.push_back(vb);
    }

    vk::DescriptorSetLayoutCreateInfo createInfo{};
    createInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
    createInfo.pBindings = vkBindings.data();

    vk::DescriptorSetLayout layout = m_device.createDescriptorSetLayout(createInfo);

    // 4. 创建 Schema
    auto schema = std::make_shared<DescriptorSetSchema>();
    schema->m_name = schemaName;
    schema->m_setIndex = setIndex;
    schema->m_layout = layout;
    schema->m_bindings = std::move(canonicalBindings);

    // 5. 存入缓存
    m_schemasByKey.emplace(std::move(key), schema);
    m_schemasByName[nameKey] = schema;

    return schema;
}

std::shared_ptr<const DescriptorSetSchema> DescriptorSetLayoutCache::getSchema(const std::string &schemaName,
                                                                               uint32_t setIndex) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string nameKey = makeNameKey(schemaName, setIndex);
    auto it = m_schemasByName.find(nameKey);
    if (it == m_schemasByName.end())
        return nullptr;

    return it->second.lock();
}

vk::DescriptorSetLayout DescriptorSetLayoutCache::getLayout(const std::string &schemaName, uint32_t setIndex) const
{
    auto schema = getSchema(schemaName, setIndex);
    if (!schema)
        return vk::DescriptorSetLayout{};
    return schema->getLayout();
}

void DescriptorSetLayoutCache::cleanup()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto &[key, schema] : m_schemasByKey)
    {
        if (schema && schema->m_layout)
        {
            m_device.destroyDescriptorSetLayout(schema->m_layout);
            schema->m_layout = vk::DescriptorSetLayout{};
        }
    }

    m_schemasByKey.clear();
    m_schemasByName.clear();
}

// =====================================================================================
// DescriptorPoolAllocator
// =====================================================================================

DescriptorPoolAllocator::DescriptorPoolAllocator(vk::Device device, DescriptorSetLayoutCache &layoutCache)
    : m_device(device), m_layoutCache(&layoutCache)
{
}

DescriptorPoolAllocator::~DescriptorPoolAllocator()
{
    cleanup();
}

vk::DescriptorSet DescriptorPoolAllocator::allocate(const std::shared_ptr<const DescriptorSetSchema> &schema)
{
    auto sets = allocate(schema, 1);
    return sets.empty() ? vk::DescriptorSet{} : sets[0];
}

std::vector<vk::DescriptorSet> DescriptorPoolAllocator::allocate(
    const std::shared_ptr<const DescriptorSetSchema> &schema, uint32_t count)
{
    if (!schema || !schema->getLayout())
    {
        throw std::runtime_error("DescriptorPoolAllocator::allocate: invalid schema or layout");
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_currentPool)
    {
        m_currentPool = acquirePool();
    }

    std::vector<vk::DescriptorSetLayout> layouts(count, schema->getLayout());
    vk::DescriptorSetAllocateInfo allocInfo{};
    allocInfo.descriptorPool = m_currentPool;
    allocInfo.descriptorSetCount = count;
    allocInfo.pSetLayouts = layouts.data();

    try
    {
        return m_device.allocateDescriptorSets(allocInfo);
    }
    catch (const vk::OutOfPoolMemoryError &)
    {
        // 当前池耗尽，换一个新池再试一次
        m_currentPool = acquirePool();
        allocInfo.descriptorPool = m_currentPool;
        return m_device.allocateDescriptorSets(allocInfo);
    }
    catch (const vk::SystemError &e)
    {
        throw std::runtime_error(
            std::string("DescriptorPoolAllocator::allocate: failed to allocate descriptor sets: ") + e.what());
    }
}

void DescriptorPoolAllocator::resetPools()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto pool : m_usedPools)
    {
        m_device.resetDescriptorPool(pool);
        m_freePools.push_back(pool);
    }
    m_usedPools.clear();
    m_currentPool = vk::DescriptorPool{};
}

void DescriptorPoolAllocator::cleanup()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto pool : m_usedPools)
    {
        m_device.destroyDescriptorPool(pool);
    }
    for (auto pool : m_freePools)
    {
        m_device.destroyDescriptorPool(pool);
    }

    m_usedPools.clear();
    m_freePools.clear();
    m_currentPool = vk::DescriptorPool{};
}

vk::DescriptorPool DescriptorPoolAllocator::acquirePool()
{
    if (!m_freePools.empty())
    {
        vk::DescriptorPool pool = m_freePools.back();
        m_freePools.pop_back();
        m_usedPools.push_back(pool);
        return pool;
    }

    // 创建新池：这里给一个通用配置，实际项目中可以根据资源规模调整
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        {vk::DescriptorType::eSampler, 512},
        {vk::DescriptorType::eCombinedImageSampler, 1024},
        {vk::DescriptorType::eSampledImage, 1024},
        {vk::DescriptorType::eStorageImage, 512},
        {vk::DescriptorType::eUniformTexelBuffer, 256},
        {vk::DescriptorType::eStorageTexelBuffer, 256},
        {vk::DescriptorType::eUniformBuffer, 1024},
        {vk::DescriptorType::eStorageBuffer, 1024},
        {vk::DescriptorType::eUniformBufferDynamic, 256},
        {vk::DescriptorType::eStorageBufferDynamic, 256},
        {vk::DescriptorType::eInputAttachment, 256},
    };

    vk::DescriptorPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1024;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    vk::DescriptorPool pool = m_device.createDescriptorPool(poolInfo);
    m_usedPools.push_back(pool);
    return pool;
}

// =====================================================================================
// DescriptorSetWriter
// =====================================================================================

DescriptorSetWriter::DescriptorSetWriter(vk::Device device, std::shared_ptr<const DescriptorSetSchema> schema,
                                         vk::DescriptorSet set)
    : m_device(device), m_schema(std::move(schema)), m_set(set)
{
}

DescriptorSetWriter DescriptorSetWriter::begin(vk::Device device, std::shared_ptr<const DescriptorSetSchema> schema,
                                               vk::DescriptorSet set)
{
    return DescriptorSetWriter(device, std::move(schema), set);
}

const DescriptorBindingInfo *DescriptorSetWriter::findBindingOrThrow(std::string_view bindingName) const
{
    if (!m_schema)
    {
        throw std::runtime_error("DescriptorSetWriter: schema is null");
    }
    const DescriptorBindingInfo *info = m_schema->findBinding(bindingName);
    if (!info)
    {
        throw std::runtime_error(std::string("DescriptorSetWriter: binding not found: ") + std::string(bindingName));
    }
    return info;
}

DescriptorSetWriter::BufferWrite *DescriptorSetWriter::findOrCreateBufferWrite(uint32_t binding,
                                                                               vk::DescriptorType type,
                                                                               uint32_t maxCount)
{
    for (auto &bw : m_bufferWrites)
    {
        if (bw.binding == binding)
        {
            // 若类型或 maxCount 不一致，说明 schema 被误用，直接断言或抛异常
            if (bw.type != type || bw.maxCount != maxCount)
            {
                throw std::runtime_error("DescriptorSetWriter: inconsistent BufferWrite type/maxCount");
            }
            return &bw;
        }
    }

    BufferWrite bw{};
    bw.binding = binding;
    bw.type = type;
    bw.maxCount = maxCount;
    m_bufferWrites.push_back(std::move(bw));
    return &m_bufferWrites.back();
}

DescriptorSetWriter::ImageWrite *DescriptorSetWriter::findOrCreateImageWrite(uint32_t binding, vk::DescriptorType type,
                                                                             uint32_t maxCount)
{
    for (auto &iw : m_imageWrites)
    {
        if (iw.binding == binding)
        {
            if (iw.type != type || iw.maxCount != maxCount)
            {
                throw std::runtime_error("DescriptorSetWriter: inconsistent ImageWrite type/maxCount");
            }
            return &iw;
        }
    }

    ImageWrite iw{};
    iw.binding = binding;
    iw.type = type;
    iw.maxCount = maxCount;
    m_imageWrites.push_back(std::move(iw));
    return &m_imageWrites.back();
}

DescriptorSetWriter &DescriptorSetWriter::writeBuffer(std::string_view bindingName,
                                                      const vk::DescriptorBufferInfo &bufferInfo)
{
    const DescriptorBindingInfo *bindingInfo = findBindingOrThrow(bindingName);
    BufferWrite *bw =
        findOrCreateBufferWrite(bindingInfo->binding, bindingInfo->descriptorType, bindingInfo->descriptorCount);

    // 单个写入：覆盖之前内容，只保留一个
    bw->infos.clear();
    bw->infos.push_back(bufferInfo);
    return *this;
}

DescriptorSetWriter &DescriptorSetWriter::writeBuffers(std::string_view bindingName,
                                                       const std::vector<vk::DescriptorBufferInfo> &bufferInfos)
{
    const DescriptorBindingInfo *bindingInfo = findBindingOrThrow(bindingName);
    BufferWrite *bw =
        findOrCreateBufferWrite(bindingInfo->binding, bindingInfo->descriptorType, bindingInfo->descriptorCount);

    // 覆盖之前内容，然后按“末尾优先”的策略截断到 maxCount
    bw->infos = bufferInfos;
    if (bw->infos.size() > bw->maxCount)
    {
        // 保留最后 maxCount 个
        bw->infos.erase(bw->infos.begin(), bw->infos.end() - static_cast<std::ptrdiff_t>(bw->maxCount));
    }
    return *this;
}

DescriptorSetWriter &DescriptorSetWriter::writeImage(std::string_view bindingName,
                                                     const vk::DescriptorImageInfo &imageInfo)
{
    const DescriptorBindingInfo *bindingInfo = findBindingOrThrow(bindingName);
    ImageWrite *iw =
        findOrCreateImageWrite(bindingInfo->binding, bindingInfo->descriptorType, bindingInfo->descriptorCount);

    iw->infos.clear();
    iw->infos.push_back(imageInfo);
    return *this;
}

DescriptorSetWriter &DescriptorSetWriter::writeImages(std::string_view bindingName,
                                                      const std::vector<vk::DescriptorImageInfo> &imageInfos)
{
    const DescriptorBindingInfo *bindingInfo = findBindingOrThrow(bindingName);
    ImageWrite *iw =
        findOrCreateImageWrite(bindingInfo->binding, bindingInfo->descriptorType, bindingInfo->descriptorCount);

    iw->infos = imageInfos;
    if (iw->infos.size() > iw->maxCount)
    {
        iw->infos.erase(iw->infos.begin(), iw->infos.end() - static_cast<std::ptrdiff_t>(iw->maxCount));
    }
    return *this;
}

// ------------------ Managed* 便捷接口 ------------------

DescriptorSetWriter &DescriptorSetWriter::writeBuffer(std::string_view bindingName, const ManagedBuffer &buffer,
                                                      vk::DeviceSize offset)
{
    if (!buffer)
    {
        throw std::runtime_error("DescriptorSetWriter::writeBuffer: ManagedBuffer is null");
    }

    vk::DescriptorBufferInfo info{};
    info.buffer = buffer.getBuffer();
    info.offset = offset;
    info.range = VK_WHOLE_SIZE;

    return writeBuffer(bindingName, info);
}

DescriptorSetWriter &DescriptorSetWriter::writeImage(std::string_view bindingName, const ManagedImage &image,
                                                     vk::Sampler sampler, vk::ImageLayout layout)
{
    if (!image)
    {
        throw std::runtime_error("DescriptorSetWriter::writeImage: ManagedImage is null");
    }

    vk::DescriptorImageInfo info{};
    info.sampler = sampler;
    info.imageView = image.getView();
    info.imageLayout = layout;

    return writeImage(bindingName, info);
}

DescriptorSetWriter &DescriptorSetWriter::writeSampledImage(std::string_view bindingName, const ManagedImage &image,
                                                            const ManagedSampler &sampler, vk::ImageLayout layout)
{
    if (!image)
    {
        throw std::runtime_error("DescriptorSetWriter::writeSampledImage: ManagedImage is null");
    }
    if (!sampler)
    {
        throw std::runtime_error("DescriptorSetWriter::writeSampledImage: ManagedSampler is null");
    }

    vk::DescriptorImageInfo info{};
    info.sampler = sampler.getSampler();
    info.imageView = image.getView();
    info.imageLayout = layout;

    return writeImage(bindingName, info);
}

DescriptorSetWriter &DescriptorSetWriter::writeStorageImage(std::string_view bindingName, const ManagedImage &image,
                                                            vk::ImageLayout layout)
{
    if (!image)
    {
        throw std::runtime_error("DescriptorSetWriter::writeStorageImage: ManagedImage is null");
    }

    vk::DescriptorImageInfo info{};
    info.sampler = vk::Sampler{}; // storage image 通常不使用 sampler
    info.imageView = image.getView();
    info.imageLayout = layout;

    return writeImage(bindingName, info);
}

void DescriptorSetWriter::update()
{
    std::vector<vk::WriteDescriptorSet> writes;
    writes.reserve(m_bufferWrites.size() + m_imageWrites.size());

    // Buffer 写入
    for (auto &bw : m_bufferWrites)
    {
        if (bw.infos.empty())
            continue;

        const uint32_t count = std::min<uint32_t>(bw.maxCount, static_cast<uint32_t>(bw.infos.size()));
        const vk::DescriptorBufferInfo *pInfos = bw.infos.data() + (bw.infos.size() - count); // 末尾的 count 个

        vk::WriteDescriptorSet write{};
        write.dstSet = m_set;
        write.dstBinding = bw.binding;
        write.dstArrayElement = 0;
        write.descriptorType = bw.type;
        write.descriptorCount = count;
        write.pBufferInfo = pInfos;

        writes.push_back(write);
    }

    // Image 写入
    for (auto &iw : m_imageWrites)
    {
        if (iw.infos.empty())
            continue;

        const uint32_t count = std::min<uint32_t>(iw.maxCount, static_cast<uint32_t>(iw.infos.size()));
        const vk::DescriptorImageInfo *pInfos = iw.infos.data() + (iw.infos.size() - count); // 末尾的 count 个

        vk::WriteDescriptorSet write{};
        write.dstSet = m_set;
        write.dstBinding = iw.binding;
        write.dstArrayElement = 0;
        write.descriptorType = iw.type;
        write.descriptorCount = count;
        write.pImageInfo = pInfos;

        writes.push_back(write);
    }

    if (!writes.empty())
    {
        m_device.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // 清空缓存，方便下一轮复用
    m_bufferWrites.clear();
    m_imageWrites.clear();
}

} // namespace vkcore
