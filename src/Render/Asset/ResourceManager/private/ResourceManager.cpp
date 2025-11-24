#include "ResourceManager.hpp"
#include "Utils.hpp"
#include "vkcore.hpp"
#include <filesystem>
#include <iostream>
#include <thread>

namespace asset
{
ResourceManager::ResourceManager(vkcore::VkContext &context) : m_context(&context)
{
    m_layoutCache = new vkcore::DescriptorSetLayoutCache(context.getDevice());
    m_poolAllocator = new vkcore::DescriptorPoolAllocator(context.getDevice(), *m_layoutCache);

    std::vector<MeshData> cubeMesh = getDefaultCubeMesh();
    m_meshCache.loadedMeshes.try_emplace("default_cube", std::make_shared<std::vector<MeshData>>(std::move(cubeMesh)));
    TextureData whiteTexture = getDefaultWhiteTexture();
    m_textureCache.loadedTextures.try_emplace("default_white", std::make_shared<TextureData>(std::move(whiteTexture)));
}

ResourceManager::~ResourceManager()
{
    cleanup();
}

void ResourceManager::cleanup()
{
    // 释放描述符集布局和池
    if (m_poolAllocator)
    {
        m_poolAllocator->cleanup();
        delete m_poolAllocator;
        m_poolAllocator = nullptr;
    }
    if (m_layoutCache)
    {
        m_layoutCache->cleanup();
        delete m_layoutCache;
        m_layoutCache = nullptr;
    }

    // 清理其他资源（网格、纹理等）
    {
        std::lock_guard<std::mutex> lock(m_meshCache.mutex);
        m_meshCache.loadedMeshes.clear();
        m_meshCache.loadingMeshes.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_textureCache.mutex);
        m_textureCache.loadedTextures.clear();
        m_textureCache.loadingTextures.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_shaderCache.mutex);
        m_shaderCache.loadedShaders.clear();
        m_shaderCache.loadingShaders.clear();
    }
}
std::string ResourceManager::loadMesh(const std::filesystem::path &filepath)
{
    if (!std::filesystem::exists(filepath))
    {
        throw std::runtime_error("Mesh file does not exist: " + filepath.string());
    }

    const std::string resourceId = normalizeResourcePath(filepath);

    {
        std::lock_guard<std::mutex> lock(m_meshCache.mutex);
        if (m_meshCache.loadedMeshes.find(resourceId) != m_meshCache.loadedMeshes.end())
        {
            return resourceId;
        }
    }

    auto meshData = ModelLoader::loadFromFile(filepath);
    if (meshData.empty())
    {
        throw std::runtime_error("Mesh file contains no mesh data: " + filepath.string());
    }

    auto meshPtr = std::make_shared<std::vector<MeshData>>(std::move(meshData));
    {
        std::lock_guard<std::mutex> lock(m_meshCache.mutex);
        m_meshCache.loadedMeshes.try_emplace(resourceId, std::move(meshPtr));
    }

    return resourceId;
}

std::string ResourceManager::loadTexture(const std::filesystem::path &filepath)
{
    if (!std::filesystem::exists(filepath))
    {
        throw std::runtime_error("Texture file does not exist: " + filepath.string());
    }
    const std::string resourceId = normalizeResourcePath(filepath);

    {
        std::lock_guard<std::mutex> lock(m_textureCache.mutex);
        if (m_textureCache.loadedTextures.find(resourceId) != m_textureCache.loadedTextures.end())
        {
            return resourceId;
        }
    }
    TextureData textureData = TextureLoader::loadFromFile(filepath, 0, false);

    if (!textureData.isValid())
    {
        throw std::runtime_error("Failed to load texture: " + filepath.string());
    }
    auto texturePtr = std::make_shared<TextureData>(std::move(textureData));
    {
        std::lock_guard<std::mutex> lock(m_textureCache.mutex);
        m_textureCache.loadedTextures.try_emplace(resourceId, std::move(texturePtr));
    }
    return resourceId;
}

std::string ResourceManager::loadShader(const std::filesystem::path &filepath, std::string shaderName,
                                        bool enableComputeShader)
{
    if (!std::filesystem::exists(filepath))
    {
        throw std::runtime_error("Shader file does not exist: " + filepath.string());
    }
    std::string resourceId = normalizeResourcePath(filepath / shaderName);

    {
        std::lock_guard<std::mutex> lock(m_shaderCache.mutex);
        if (m_shaderCache.loadedShaders.find(resourceId) != m_shaderCache.loadedShaders.end())
        {
            return resourceId;
        }
    }

    std::vector<std::filesystem::path> shaderFiles;
    shaderFiles.push_back(filepath / (shaderName + ".vert.spv"));
    shaderFiles.push_back(filepath / (shaderName + ".frag.spv"));
    if (enableComputeShader)
    {
        shaderFiles.push_back(filepath / (shaderName + ".comp.spv"));
    }

    shaderProgram program;
    std::vector<std::vector<uint32_t>> spirvCodes;
    for (int i = 0; i < shaderFiles.size(); ++i)
    {
        if (!std::filesystem::exists(shaderFiles[i]))
        {
            throw std::runtime_error("Shader file does not exist: " + shaderFiles[i].string());
        }
        std::ifstream file(shaderFiles[i], std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open shader file: " + shaderFiles[i].string());
        }
        size_t fileSize = static_cast<size_t>(file.tellg());
        if (fileSize % 4 != 0)
        {
            throw std::runtime_error("Invalid SPIR-V file size: " + shaderFiles[i].string());
        }

        std::vector<uint32_t> spirvCode(fileSize / 4);
        file.seekg(0);
        file.read(reinterpret_cast<char *>(spirvCode.data()), fileSize);
        file.close();
        spirvCodes.push_back(spirvCode);
    }
    program.vertexShader =
        std::make_shared<ShaderModule>(m_context->getDevice(), spirvCodes[0], vk::ShaderStageFlagBits::eVertex);
    program.fragmentShader =
        std::make_shared<ShaderModule>(m_context->getDevice(), spirvCodes[1], vk::ShaderStageFlagBits::eFragment);
    if (enableComputeShader && spirvCodes.size() > 2)
    {
        program.computeShader =
            std::make_shared<ShaderModule>(m_context->getDevice(), spirvCodes[2], vk::ShaderStageFlagBits::eCompute);
    }
    reflectDescriptorSetLayouts(spirvCodes, shaderName);
    {
        std::lock_guard<std::mutex> lock(m_shaderCache.mutex);
        // 同时使用资源绝对路径与 shader 前缀作为键，便于 RenderPass 通过前缀查找程序
        m_shaderCache.loadedShaders.try_emplace(resourceId, program);
        m_shaderCache.loadedShaders.try_emplace(shaderName, std::move(program));
    }
    return resourceId;
}

std::shared_future<std::string> ResourceManager::loadMeshAsync(const std::filesystem::path &filepath)
{
    // 统一规范资源 ID，避免同一文件用不同写法重复加载
    const std::string resourceId = normalizeResourcePath(filepath);

    {
        std::lock_guard<std::mutex> lock(m_meshCache.mutex);

        // 1. 已经加载完成：返回一个已完成的 shared_future
        auto itLoaded = m_meshCache.loadedMeshes.find(resourceId);
        if (itLoaded != m_meshCache.loadedMeshes.end())
        {
            std::promise<std::string> p;
            p.set_value(resourceId);
            return p.get_future().share();
        }

        // 2. 正在加载：复用已有的 shared_future
        auto itLoading = m_meshCache.loadingMeshes.find(resourceId);
        if (itLoading != m_meshCache.loadingMeshes.end())
        {
            return itLoading->second;
        }
    }

    // 3. 第一次请求这个资源 → 创建异步任务
    std::packaged_task<std::string()> task([this, filepath, resourceId]() {
        // 在任务结束时，从 loadingMeshes 中自动擦除自己
        struct LoadingEraser
        {
            MeshCache &cache;
            std::string id;

            ~LoadingEraser()
            {
                std::lock_guard<std::mutex> lock(cache.mutex);
                cache.loadingMeshes.erase(id);
            }
        } eraser{m_meshCache, resourceId};

        // 加载逻辑
        return this->loadMesh(filepath);
    });

    // 从 packaged_task 拿到 future，再转成 shared_future
    std::future<std::string> fut = task.get_future();
    std::shared_future<std::string> sfut = fut.share();

    {
        std::lock_guard<std::mutex> lock(m_meshCache.mutex);
        m_meshCache.loadingMeshes.emplace(resourceId, sfut);
    }

    // 启动后台线程执行任务
    std::thread(std::move(task)).detach();

    // 返回 shared_future（拷贝一份句柄，共享同一状态）
    return sfut;
}

std::shared_future<std::string> ResourceManager::loadTextureAsync(const std::filesystem::path &filepath)
{
    const std::string resourceId = normalizeResourcePath(filepath);

    {
        std::lock_guard<std::mutex> lock(m_textureCache.mutex);
        auto itLoaded = m_textureCache.loadedTextures.find(resourceId);
        if (itLoaded != m_textureCache.loadedTextures.end())
        {
            std::promise<std::string> p;
            p.set_value(resourceId);
            return p.get_future().share();
        }

        auto itLoading = m_textureCache.loadingTextures.find(resourceId);
        if (itLoading != m_textureCache.loadingTextures.end())
        {
            return itLoading->second;
        }
    }

    std::packaged_task<std::string()> task([this, filepath, resourceId]() {
        struct LoadingEraser
        {
            TextureCache &cache;
            std::string id;
            ~LoadingEraser()
            {
                std::lock_guard<std::mutex> lock(cache.mutex);
                cache.loadingTextures.erase(id);
            }
        } eraser{m_textureCache, resourceId};

        return this->loadTexture(filepath);
    });

    auto sfut = task.get_future().share();
    {
        std::lock_guard<std::mutex> lock(m_textureCache.mutex);
        m_textureCache.loadingTextures.emplace(resourceId, sfut);
    }

    std::thread(std::move(task)).detach();
    return sfut;
}

std::shared_future<std::string> ResourceManager::loadShaderAsync(const std::filesystem::path &filepath,
                                                                 std::string shaderName, bool enableComputeShader)
{
    const std::string resourceId = normalizeResourcePath(filepath / shaderName);

    {
        std::lock_guard<std::mutex> lock(m_shaderCache.mutex);
        auto itLoaded = m_shaderCache.loadedShaders.find(resourceId);
        if (itLoaded != m_shaderCache.loadedShaders.end())
        {
            std::promise<std::string> p;
            p.set_value(resourceId);
            return p.get_future().share();
        }

        auto itLoading = m_shaderCache.loadingShaders.find(resourceId);
        if (itLoading != m_shaderCache.loadingShaders.end())
        {
            return itLoading->second;
        }
    }

    std::packaged_task<std::string()> task(
        [this, filepath, shaderName = std::move(shaderName), resourceId, enableComputeShader]() {
            struct LoadingEraser
            {
                ShaderCache &cache;
                std::string id;
                ~LoadingEraser()
                {
                    std::lock_guard<std::mutex> lock(cache.mutex);
                    cache.loadingShaders.erase(id);
                }
            } eraser{m_shaderCache, resourceId};

            return this->loadShader(filepath, shaderName, enableComputeShader);
        });

    auto sfut = task.get_future().share();
    {
        std::lock_guard<std::mutex> lock(m_shaderCache.mutex);
        m_shaderCache.loadingShaders.emplace(resourceId, sfut);
    }

    std::thread(std::move(task)).detach();
    return sfut;
}

std::shared_future<std::vector<std::string>> ResourceManager::loadMeshesAsync(
    const std::vector<std::filesystem::path> &filepaths)
{
    std::packaged_task<std::vector<std::string>()> task(
        [this, paths = std::vector<std::filesystem::path>(filepaths)]() {
            std::vector<std::shared_future<std::string>> futures;
            futures.reserve(paths.size());
            for (const auto &p : paths)
            {
                futures.push_back(this->loadMeshAsync(p));
            }

            std::vector<std::string> ids;
            ids.reserve(futures.size());
            for (auto &f : futures)
            {
                ids.push_back(f.get());
            }
            return ids;
        });

    auto sfut = task.get_future().share();
    std::thread(std::move(task)).detach();
    return sfut;
}

std::shared_future<std::vector<std::string>> ResourceManager::loadTexturesAsync(
    const std::vector<std::filesystem::path> &filepaths)
{
    std::packaged_task<std::vector<std::string>()> task(
        [this, paths = std::vector<std::filesystem::path>(filepaths)]() {
            std::vector<std::shared_future<std::string>> futures;
            futures.reserve(paths.size());
            for (const auto &p : paths)
            {
                futures.push_back(this->loadTextureAsync(p));
            }

            std::vector<std::string> ids;
            ids.reserve(futures.size());
            for (auto &f : futures)
            {
                ids.push_back(f.get());
            }
            return ids;
        });

    auto sfut = task.get_future().share();
    std::thread(std::move(task)).detach();
    return sfut;
}

std::shared_ptr<std::vector<MeshData>> ResourceManager::registerMesh(const std::string &name,
                                                                     const std::vector<Vertex> &vertices,
                                                                     const std::vector<uint32_t> &indices)
{
    auto meshVec = std::make_shared<std::vector<MeshData>>();
    MeshData mesh{};
    mesh.debugname = name;
    mesh.vertices = vertices;
    mesh.indices = indices;
    meshVec->push_back(std::move(mesh));

    std::lock_guard<std::mutex> lock(m_meshCache.mutex);
    auto [it, inserted] = m_meshCache.loadedMeshes.try_emplace(name, meshVec);
    if (!inserted)
    {
        return it->second;
    }
    return meshVec;
}

bool ResourceManager::unloadMesh(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_meshCache.mutex);
    m_meshCache.loadingMeshes.erase(name);
    return m_meshCache.loadedMeshes.erase(name) > 0;
}

bool ResourceManager::unloadTexture(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_textureCache.mutex);
    m_textureCache.loadingTextures.erase(name);
    return m_textureCache.loadedTextures.erase(name) > 0;
}

//================================================================//
// SPIR-V 反射相关实现
//================================================================//
void ResourceManager::reflectSingleShaderModule(
    const std::vector<uint32_t> &spirv, vk::ShaderStageFlagBits stage,
    std::unordered_map<uint32_t, std::vector<vkcore::DescriptorBindingInfo>> &outSets)
{
    if (spirv.empty())
        return;
    SpvReflectShaderModule module{};
    SpvReflectResult result = spvReflectCreateShaderModule(spirv.size() * sizeof(uint32_t), spirv.data(), &module);
    if (result != SPV_REFLECT_RESULT_SUCCESS)
    {
        throw std::runtime_error("Failed to create SPIR-V reflection module");
    }
    ReflectModuleGuard guard{&module};

    //枚举描述符绑定
    uint32_t setCount = 0;
    result = spvReflectEnumerateDescriptorSets(&module, &setCount, nullptr);
    if (result != SPV_REFLECT_RESULT_SUCCESS)
    {
        throw std::runtime_error("Failed to enumerate descriptor sets");
    }

    std::vector<SpvReflectDescriptorSet *> sets(setCount);
    result = spvReflectEnumerateDescriptorSets(&module, &setCount, sets.data());
    if (result != SPV_REFLECT_RESULT_SUCCESS)
    {
        throw std::runtime_error("Failed to enumerate descriptor sets");
    }

    for (SpvReflectDescriptorSet *refSet : sets)
    {
        const uint32_t setNumber = refSet->set;

        auto &vec = outSets[setNumber];
        vec.reserve(vec.size() + refSet->binding_count);

        for (uint32_t i = 0; i < refSet->binding_count; ++i)
        {
            SpvReflectDescriptorBinding *rb = refSet->bindings[i];

            vkcore::DescriptorBindingInfo bindingInfo{};
            bindingInfo.name = rb->name ? rb->name : "";
            bindingInfo.binding = rb->binding;
            bindingInfo.descriptorType = ToVkDescriptorType(rb->descriptor_type);
            bindingInfo.descriptorCount = rb->count;
            bindingInfo.stageFlags = stage;

            vec.push_back(std::move(bindingInfo));
        }
    }
}

std::unordered_map<uint32_t, std::vector<vkcore::DescriptorBindingInfo>> ResourceManager::mergeReflectionResults(
    const std::vector<std::unordered_map<uint32_t, std::vector<vkcore::DescriptorBindingInfo>>> &perModuleData)
{
    std::unordered_map<uint32_t, std::vector<vkcore::DescriptorBindingInfo>> merged;

    for (const auto &setMap : perModuleData)
    {
        for (const auto &setPair : setMap)
        {
            uint32_t setIndex = setPair.first;
            const auto &bindings = setPair.second;

            auto &dstVec = merged[setIndex];

            for (const auto &b : bindings)
            {
                // 使用 binding + descriptorType 作为匹配条件
                auto it =
                    std::find_if(dstVec.begin(), dstVec.end(), [&](const vkcore::DescriptorBindingInfo &existing) {
                        return existing.binding == b.binding && existing.descriptorType == b.descriptorType;
                    });

                if (it == dstVec.end())
                {
                    // 不存在就新增
                    dstVec.push_back(b);
                }
                else
                {
                    // descriptorCount 必须一致
                    if (it->descriptorCount != b.descriptorCount)
                    {
                        throw std::runtime_error("Descriptor count mismatch between shader stages at set " +
                                                 std::to_string(setIndex) + ", binding " + std::to_string(b.binding) +
                                                 " (" + b.name + ")");
                    }

                    // 合并 stage flags（而不是检查是否一致）
                    it->stageFlags |= b.stageFlags;

                    // 如果 name 不一致，仅保留第一个
                    if (it->name != b.name)
                    {
                        // 可选：输出警告日志帮助调试
                        // std::cerr << "[Reflection] Merged name mismatch at set "
                        //           << setIndex << " binding " << b.binding
                        //           << ": '" << it->name << "' vs '" << b.name << "'\n";
                    }
                }
            }
        }
    }

    // 排序绑定（Vulkan 要求 binding 连续增长时更友好）
    for (auto &pair : merged)
    {
        auto &bindings = pair.second;
        std::sort(bindings.begin(), bindings.end(),
                  [](const vkcore::DescriptorBindingInfo &a, const vkcore::DescriptorBindingInfo &b) {
                      return a.binding < b.binding;
                  });
    }

    return merged;
}

void ResourceManager::registerDescriptorLayouts(
    const std::unordered_map<uint32_t, std::vector<vkcore::DescriptorBindingInfo>> &finalSets,
    const std::string &shaderPrefix)
{
    if (!m_layoutCache)
    {
        // 如果 layoutCache 还没设置，就先悄悄溜过去
        return;
    }

    for (const auto &pair : finalSets)
    {
        uint32_t setIndex = pair.first;
        const auto &bindings = pair.second;

        if (bindings.empty())
            continue;

        // 因为 mergeReflectionResults 已经排序，这里不需要再 sort
        m_layoutCache->registerSetLayout(shaderPrefix, setIndex, bindings);
    }
}

void ResourceManager::reflectDescriptorSetLayouts(const std::vector<std::vector<uint32_t>> &spirvCodes,
                                                  const std::string &shaderPrefix)
{
    if (spirvCodes.empty())
        return;

    std::vector<std::unordered_map<uint32_t, std::vector<vkcore::DescriptorBindingInfo>>> perModuleData;
    // 反射每个模块

    auto addModule = [&](size_t index, vk::ShaderStageFlagBits stage) {
        if (index >= spirvCodes.size())
            return;
        const auto &spv = spirvCodes[index];
        if (spv.empty())
            return;

        std::unordered_map<uint32_t, std::vector<vkcore::DescriptorBindingInfo>> oneModuleSets;
        reflectSingleShaderModule(spv, stage, oneModuleSets);
        perModuleData.emplace_back(std::move(oneModuleSets));
    };

    addModule(0, vk::ShaderStageFlagBits::eVertex);
    if (spirvCodes.size() > 1)
        addModule(1, vk::ShaderStageFlagBits::eFragment);
    if (spirvCodes.size() > 2)
        addModule(2, vk::ShaderStageFlagBits::eCompute);

    if (perModuleData.empty())
        return;

    auto finalSets = mergeReflectionResults(perModuleData);
    registerDescriptorLayouts(finalSets, shaderPrefix);
}

std::shared_ptr<std::vector<MeshData>> ResourceManager::getMesh(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_meshCache.mutex);
    auto it = m_meshCache.loadedMeshes.find(name);
    if (it != m_meshCache.loadedMeshes.end())
    {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<TextureData> ResourceManager::getTexture(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_textureCache.mutex);
    auto it = m_textureCache.loadedTextures.find(name);
    if (it != m_textureCache.loadedTextures.end())
    {
        return it->second;
    }
    return nullptr;
}

shaderProgram ResourceManager::getShaderprogram(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_shaderCache.mutex);
    // 优先按传入的前缀查找，其次尝试归一化路径键以兼容旧调用
    if (auto it = m_shaderCache.loadedShaders.find(name); it != m_shaderCache.loadedShaders.end())
    {
        return it->second;
    }
    const std::string normalized = normalizeResourcePath(name);
    if (auto it = m_shaderCache.loadedShaders.find(normalized); it != m_shaderCache.loadedShaders.end())
    {
        return it->second;
    }
    return shaderProgram{};
}

std::vector<vk::DescriptorSet> ResourceManager::getOrAllocateDescriptorSet(
    std::vector<std::shared_ptr<const vkcore::DescriptorSetSchema>> schemas, const std::string &ShaderPrefix)
{
    {
        std::lock_guard<std::mutex> lock(m_descriptorSetMutex);
        auto it = m_descriptorSets.find(ShaderPrefix);
        if (it != m_descriptorSets.end())
        {
            return it->second;
        }
    }

    if (schemas.empty())
    {
        throw std::runtime_error("No descriptor set schemas provided for allocation with prefix: " + ShaderPrefix);
    }
    std::vector<vk::DescriptorSet> allocatedSets;
    allocatedSets.reserve(schemas.size());
    for (uint32_t i = 0; i < schemas.size(); ++i)
    {
        auto schema = schemas[i];
        if (!schema)
        {
            throw std::runtime_error("Invalid descriptor set schema at index " + std::to_string(i) +
                                     " for allocation with prefix: " + ShaderPrefix);
        }
        allocatedSets.push_back(std::move(m_poolAllocator->allocate(schema)));
    }
    {
        std::lock_guard<std::mutex> lock(m_descriptorSetMutex);
        m_descriptorSets[ShaderPrefix] = allocatedSets;
    }
    return allocatedSets;
}

std::vector<std::shared_ptr<const vkcore::DescriptorSetSchema>> ResourceManager::getShaderDescriptorSchemas(
    const std::string &shaderPrefix) const
{
    std::vector<std::shared_ptr<const vkcore::DescriptorSetSchema>> schemas;

    // Vulkan: set index 范围通常为 0~3，但我们可扩展
    for (uint32_t set = 0; set < 8; ++set)
    {
        auto schema = m_layoutCache->getSchema(shaderPrefix, set);
        if (schema)
        {
            schemas.push_back(schema);
#ifdef ENABLE_DEBUG_LOG
            std::cout << "=== Descriptor Set Layout Debug ===\n";
            std::cout << "Shader: " << shaderPrefix << " Set: " << set << "\n";
            for (auto &b : schema->getBindings())
            {
                std::cout << "Binding " << b.binding << " Name: " << b.name
                          << " Type: " << vk::to_string(b.descriptorType) << " Count: " << b.descriptorCount
                          << " StageFlags: " << vk::to_string(b.stageFlags) << "\n";
            }
            std::cout << "===================================\n";
#endif
        }
    }

    return schemas;
}

std::vector<MeshData> ResourceManager::getDefaultCubeMesh(float size, glm::vec4 color)
{
    return {ModelLoader::createCube(size, color)};
}

TextureData ResourceManager::getDefaultWhiteTexture(int width, int height, const std::array<unsigned char, 4> &color)
{
    return TextureLoader::createSolidColor(width, height, color);
}

TextureData ResourceManager::getDefaultCheckerboardTexture(int width, int height, int checkerSize,
                                                           const std::array<unsigned char, 4> &color1,
                                                           const std::array<unsigned char, 4> &color2)
{
    return TextureLoader::createCheckerboard(width, height, checkerSize, color1, color2);
}

} // namespace asset
