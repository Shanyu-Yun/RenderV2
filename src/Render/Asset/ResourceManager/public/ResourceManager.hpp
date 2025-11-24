/**
 * @file ResourceManager.hpp
 * @author Summer
 * @brief 资源管理器，负责网格、纹理和着色器的加载与管理
 *
 * 该文件提供了统一的资源管理接口，包括：
 * - 同步和异步资源加载
 * - 资源缓存和去重
 * - GPU资源创建与管理
 * - Mipmap生成
 * - 描述符集管理
 *
 * @version 1.0
 * @date 2025-11-23
 */

#pragma once

#include "ResourceManagerUtils.hpp"
#include "ResourceType.hpp"
#include <array>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <spirv_reflect.h>
#include <string>
#include <unordered_map>

namespace vkcore
{
struct DescriptorBindingInfo;
class DescriptorSetSchema;
class DescriptorSetLayoutCache;
class DescriptorPoolAllocator;
class DescriptorSetWriter;

class VkResourceAllocator;
class TransferManager;
class VkContext;
} // namespace vkcore

namespace asset
{

/**
 * @class ResourceManager
 * @brief 资源管理器核心类，提供资源加载、缓存和管理功能
 *
 * @details
 * ResourceManager 负责管理渲染所需的所有资源，包括：
 * - 3D模型网格（Mesh）：支持OBJ、STL等格式
 * - 纹理（Texture）：支持PNG、JPG、HDR等格式，支持Mipmap生成
 * - 着色器（Shader）：支持SPIR-V着色器加载
 *
 * 主要特性：
 * - 线程安全的资源加载和访问
 * - 异步加载支持，避免阻塞主线程
 * - 自动资源去重和缓存
 * - 与Vulkan资源管理器集成
 * - 支持描述符集的创建和管理
 *
 * @note 该类不可拷贝和移动
 */
class ResourceManager
{
  public:
    /**
     * @brief 构造资源管理器
     * @param context Vulkan上下文引用
     * @param allocator Vulkan资源分配器引用
     * @param transferManager 传输管理器引用，用于数据上传和Mipmap生成
     *
     * @note 资源管理器依赖这些组件的生命周期，调用者需确保它们在ResourceManager之前销毁
     */
    ResourceManager(vkcore::VkContext &context);

    /**
     * @brief 析构函数，自动清理所有资源
     */
    ~ResourceManager();

    /** 禁用拷贝与移动 */
    ResourceManager(const ResourceManager &) = delete;
    ResourceManager &operator=(const ResourceManager &) = delete;
    ResourceManager(ResourceManager &&) = delete;
    ResourceManager &operator=(ResourceManager &&) = delete;

    /**
     * @brief 清理所有已加载的资源
     * @details 释放所有网格、纹理、着色器和描述符资源，析构函数会自动调用
     */
    void cleanup();

    // ==================== 同步资源加载 ====================

    /**
     * @brief 同步加载网格文件
     * @param filepath 网格文件路径（支持OBJ、STL等格式）
     * @return 资源标识符（通常为文件名），用于后续获取资源
     * @throws std::runtime_error 如果文件不存在或加载失败
     *
     * @note 此函数会阻塞直到加载完成
     */
    std::string loadMesh(const std::filesystem::path &filepath);

    /**
     * @brief 同步加载纹理文件
     * @param filepath 纹理文件路径（支持PNG、JPG、HDR等格式）
     * @return 资源标识符（通常为文件名），用于后续获取资源
     * @throws std::runtime_error 如果文件不存在或加载失败
     *
     * @note 此函数会阻塞直到加载完成，包括GPU上传和Mipmap生成
     */
    std::string loadTexture(const std::filesystem::path &filepath);

    /**
     * @brief 同步加载着色器程序
     *
     * @param filepath 着色器文件夹
     * @param shaderName 着色器名称
     * @param enableComputeShader 是否启用计算着色器
     * @return std::string 着色器标识符
     */
    std::string loadShader(const std::filesystem::path &filepath, std::string shaderName,
                           bool enableComputeShader = false);
    // ==================== 异步资源加载 ====================

    /**
     * @brief 异步加载网格文件
     * @param filepath 网格文件路径
     * @return std::shared_future<std::string> 异步任务，可通过future获取资源标识符
     *
     * @note 加载在后台线程执行，不会阻塞调用线程
     */
    std::shared_future<std::string> loadMeshAsync(const std::filesystem::path &filepath);

    /**
     * @brief 异步加载纹理文件
     * @param filepath 纹理文件路径
     * @return std::shared_future<std::string> 异步任务，可通过future获取资源标识符
     *
     * @note 加载在后台线程执行，包括GPU上传和Mipmap生成
     */
    std::shared_future<std::string> loadTextureAsync(const std::filesystem::path &filepath);

    /**
     * @brief 异步加载着色器程序
     * @param filepath 着色器文件夹
     * @param shaderName 着色器名称
     * @param enableComputeShader 是否启用计算着色器（默认false）
     * @return std::shared_future<std::string> 异步任务，返回着色器标识符
     *
     * @note 支持顶点、片段和计算着色器的加载
     * shaderName代表着色器文件的前缀，例如传入"PBR_Mesh"会加载"PBR_Mesh.vert.spv"和"PBR_Mesh.frag.spv"，
     * 如果enableComputeShader为true，还会加载"PBR_Mesh.comp.spv"
     */
    std::shared_future<std::string> loadShaderAsync(const std::filesystem::path &filepath, std::string shaderName,
                                                    bool enableComputeShader = false);

    /**
     * @brief 批量异步加载多个网格文件
     * @param filepaths 网格文件路径列表
     * @return std::shared_future<std::vector<std::string>> 异步任务，返回所有资源标识符列表
     *
     * @note 所有网格并行加载，适合场景初始化时批量加载资源
     */
    std::shared_future<std::vector<std::string>> loadMeshesAsync(const std::vector<std::filesystem::path> &filepaths);
    /**
     * @brief 批量异步加载多个纹理文件
     * @param filepaths 纹理文件路径列表
     * @return std::shared_future<std::vector<std::string>> 异步任务，返回所有资源标识符列表
     *
     * @note 所有纹理并行加载，包括GPU上传和Mipmap生成
     */
    std::shared_future<std::vector<std::string>> loadTexturesAsync(const std::vector<std::filesystem::path> &filepaths);

    // ==================== 资源注册与获取 ====================

    /**
     * @brief 手动注册网格数据（用于运行时生成的几何体）
     * @param name 网格名称（用作标识符）
     * @param vertices 顶点数据
     * @param indices 索引数据
     * @return 网格数据的共享指针
     *
     * @note 适用于程序化生成的几何体，如立方体、球体等
     */
    std::shared_ptr<std::vector<MeshData>> registerMesh(const std::string &name, const std::vector<Vertex> &vertices,
                                                        const std::vector<uint32_t> &indices);

    /**
     * @brief 根据名称获取网格数据
     * @param name 网格标识符
     * @return 网格数据的共享指针，如果不存在则返回nullptr
     */
    std::shared_ptr<std::vector<MeshData>> getMesh(const std::string &name);

    /**
     * @brief 根据名称获取纹理数据
     * @param name 纹理标识符
     * @return 纹理数据的共享指针，如果不存在则返回nullptr
     */
    std::shared_ptr<TextureData> getTexture(const std::string &name);

    /**
     * @brief 根据名称获取着色器程序
     * @param name 着色器标识符
     * @return 着色器程序结构体，包含顶点、片段和计算着色器
     */
    shaderProgram getShaderprogram(const std::string &name);

    // ==================== 资源卸载 ====================

    /**
     * @brief 卸载指定的网格资源
     * @param name 网格标识符
     * @return true 如果成功卸载，false 如果资源不存在
     *
     * @note 卸载后该资源将从缓存中移除，GPU资源也会被释放
     */
    bool unloadMesh(const std::string &name);

    /**
     * @brief 卸载指定的纹理资源
     * @param name 纹理标识符
     * @return true 如果成功卸载，false 如果资源不存在
     *
     * @note 卸载后该资源将从缓存中移除，GPU资源也会被释放
     */
    bool unloadTexture(const std::string &name);

    // ==================== 描述符管理 ====================

    /**
     * @brief 获取或分配描述符集
     *
     * @param schemas 描述符集模式列表，定义了绑定布局
     * @param ShaderPrefix 着色器前缀，用于标识描述符集
     * @return std::vector<vk::DescriptorSet>
     */
    std::vector<vk::DescriptorSet> getOrAllocateDescriptorSet(
        std::vector<std::shared_ptr<const vkcore::DescriptorSetSchema>> schemas, const std::string &ShaderPrefix);

    /**
     * @brief 获取着色器对应的描述符集模式列表
     *
     * @param shaderPrefix 着色器前缀，用于标识描述符集
     * @return std::vector<std::shared_ptr<const vkcore::DescriptorSetSchema>> 描述符集模式列表
     */
    std::vector<std::shared_ptr<const vkcore::DescriptorSetSchema>> getShaderDescriptorSchemas(
        const std::string &shaderPrefix) const;

    // ==================== 默认资源的创建和管理 ====================
    std::vector<MeshData> getDefaultCubeMesh(float size = 1.0f, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});

    TextureData getDefaultWhiteTexture(int width = 4, int height = 4,
                                       const std::array<unsigned char, 4> &color = {255, 255, 255, 255});

    TextureData getDefaultCheckerboardTexture(int width, int height, int checkerSize = 8,
                                              const std::array<unsigned char, 4> &color1 = {255, 255, 255, 255},
                                              const std::array<unsigned char, 4> &color2 = {0, 0, 0, 255});

  private:
    /**
     * @struct MeshCache
     * @brief 网格资源缓存结构
     * @details 包含已加载的网格和正在加载的网格，提供线程安全的访问
     */
    struct MeshCache
    {
        std::mutex mutex; ///< 互斥锁，保护缓存访问
        std::unordered_map<std::string, std::shared_ptr<std::vector<MeshData>>> loadedMeshes; ///< 已加载的网格缓存
        std::unordered_map<std::string, std::shared_future<std::string>> loadingMeshes; ///< 正在加载的网格任务
    };

    /**
     * @struct TextureCache
     * @brief 纹理资源缓存结构
     * @details 包含已加载的纹理和正在加载的纹理，提供线程安全的访问
     */
    struct TextureCache
    {
        std::mutex mutex; ///< 互斥锁，保护缓存访问
        std::unordered_map<std::string, std::shared_ptr<TextureData>> loadedTextures;     ///< 已加载的纹理缓存
        std::unordered_map<std::string, std::shared_future<std::string>> loadingTextures; ///< 正在加载的纹理任务
    };

    /**
     * @struct ShaderCache
     * @brief 着色器资源缓存结构
     * @details 包含已加载的着色器和正在加载的着色器，提供线程安全的访问
     */
    struct ShaderCache
    {
        std::mutex mutex;                                             ///< 互斥锁，保护缓存访问
        std::unordered_map<std::string, shaderProgram> loadedShaders; ///< 已加载的着色器缓存
        std::unordered_map<std::string, std::shared_future<std::string>> loadingShaders; ///< 正在加载的着色器任务
    };

    /**
     * @struct ReflectModuleGuard
     * @brief SPIR-V反射模块的RAII管理器
     * @details 自动释放SPIR-V反射模块资源，防止内存泄漏
     */
    struct ReflectModuleGuard
    {
        SpvReflectShaderModule *mod = nullptr;
        ~ReflectModuleGuard()
        {
            if (mod)
            {
                spvReflectDestroyShaderModule(mod);
            }
        }
    };

  private:
    vkcore::VkContext *m_context = nullptr; ///< Vulkan上下文指针

    MeshCache m_meshCache;       ///< 网格资源缓存
    TextureCache m_textureCache; ///< 纹理资源缓存
    ShaderCache m_shaderCache;   ///< 着色器资源缓存

    vkcore::DescriptorSetLayoutCache *m_layoutCache = nullptr;  ///< 描述符集布局缓存
    vkcore::DescriptorPoolAllocator *m_poolAllocator = nullptr; ///< 描述符池分配器

    std::mutex m_descriptorSetMutex; ///< 互斥锁，保护描述符集缓存访问
    std::unordered_map<std::string, std::vector<vk::DescriptorSet>> m_descriptorSets; ///< 已分配的描述符集缓存

  private:
    /**
     * @brief 反射单个着色器模块的资源绑定信息
     *
     * @param spirv SPIR-V字节码数据
     * @param stage 着色器阶段
     * @param outSets 输出的描述符集绑定信息映射
     */
    void reflectSingleShaderModule(const std::vector<uint32_t> &spirv, vk::ShaderStageFlagBits stage,
                                   std::unordered_map<uint32_t, std::vector<vkcore::DescriptorBindingInfo>> &outSets);

    /**
     * @brief 合并多个着色器模块的反射结果
     *
     * @param perModuleData 各个着色器模块的反射数据
     * @return std::unordered_map<uint32_t, std::vector<DescriptorBindingInfo>>
     */
    std::unordered_map<uint32_t, std::vector<vkcore::DescriptorBindingInfo>> mergeReflectionResults(
        const std::vector<std::unordered_map<uint32_t, std::vector<vkcore::DescriptorBindingInfo>>> &perModuleData);

    /**
     * @brief 注册描述符集布局到缓存
     *
     * @param finalSets 合并后的描述符集绑定信息
     * @param shaderPrefix 着色器前缀，用于标识描述符集
     */
    void registerDescriptorLayouts(
        const std::unordered_map<uint32_t, std::vector<vkcore::DescriptorBindingInfo>> &finalSets,
        const std::string &shaderPrefix);

    /**
     * @brief 反射着色器程序的描述符集布局
     *
     * @param spirvCodes 单个着色器程序的SPIR-V字节码
     * @param resourceId 资源标识符
     */
    void reflectDescriptorSetLayouts(const std::vector<std::vector<uint32_t>> &spirvCodes,
                                     const std::string &resourceId);
};

} // namespace asset