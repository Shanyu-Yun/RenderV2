#pragma once

#include "Descriptor.hpp"
#include "ResourceManager.hpp"
#include "SceneTypes.hpp"
#include "TransferManager.hpp"
#include "VkContext.hpp"
#include "VkResource.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace renderer
{

/**
 * @brief 描述渲染器初始化阶段需要的全局资源。
 *
 * 该结构体列出了需要通过 @ref asset::ResourceManager 预加载的网格、纹理和着色器，
 * 便于在渲染启动前一次性准备好 GPU 资源和描述符布局。
 */
struct RendererGlobalResources
{
    /// 需要加载的网格文件列表。
    std::vector<std::filesystem::path> meshFiles;
    /// 需要加载的纹理文件列表。
    std::vector<std::filesystem::path> textureFiles;

    /// 着色器加载请求。
    struct ShaderRequest
    {
        std::filesystem::path directory; ///< 着色器所在文件夹。
        std::string name;                ///< 着色器前缀，例如 "PBR_Mesh"。
        bool enableCompute{false};       ///< 是否同时加载计算着色器。
    };

    /// 需要加载的着色器列表。
    std::vector<ShaderRequest> shaders;
};

/**
 * @brief 每帧资源的创建参数。
 *
 * 用于描述 UBO 大小、关联的着色器前缀以及帧并发数量，方便 RendererResourceService
 * 按需分配缓冲与描述符集。
 */
struct FrameResourceDefinition
{
    std::string shaderPrefix; ///< 关联的着色器前缀，用于查询描述符布局。
    vk::DeviceSize cameraBufferSize{sizeof(asset::CameraUBO)}; ///< 相机 UBO 大小。
    vk::DeviceSize lightBufferSize{sizeof(asset::LightUBO)};   ///< 光源 UBO 大小。
    uint32_t framesInFlight{3};                                ///< 帧并发数量。
};

/**
 * @brief 单帧持有的 GPU 资源。
 *
 * 包含相机与光源 UBO、对应的描述符模式与实例，方便在录制命令前更新数据并写入描述符。
 */
struct PerFrameGpuResources
{
    vkcore::ManagedBuffer cameraBuffer; ///< 相机数据缓冲。
    vkcore::ManagedBuffer lightBuffer;  ///< 光源数据缓冲。

    /// 由 ResourceManager 提供的描述符布局（按 set index 顺序）。
    std::vector<std::shared_ptr<const vkcore::DescriptorSetSchema>> descriptorSchemas;
    /// 与布局一一对应的 DescriptorSet。
    std::vector<vk::DescriptorSet> descriptorSets;
};

/**
 * @class RendererResourceService
 * @brief 渲染资源调度与上传服务。
 *
 * 该服务封装 ResourceManager、TransferManager 与 VkContext 的常见协作逻辑：
 * - 预加载全局资源（网格、纹理、着色器）
 * - 按帧分配 GPU Buffer 并获取描述符集
 * - 通过 TransferManager 上传相机、光照等 UBO 数据
 * - 使用 DescriptorSetWriter 便捷写入 GPU 资源到 DescriptorSet
 */
class RendererResourceService
{
  public:
    RendererResourceService(asset::ResourceManager &resourceManager, vkcore::TransferManager &transferManager,
                            vkcore::VkResourceAllocator &allocator, vkcore::VkContext &context);

    RendererResourceService(const RendererResourceService &) = delete;
    RendererResourceService &operator=(const RendererResourceService &) = delete;

    /**
     * @brief 按需预加载全局资源。
     * @param resources 描述需要加载的网格、纹理与着色器。
     */
    void preloadGlobalResources(const RendererGlobalResources &resources);

    /**
     * @brief 创建单帧 GPU 资源。
     * @param definition 帧资源描述。
     * @return 已分配的帧资源。
     */
    PerFrameGpuResources createPerFrameResources(const FrameResourceDefinition &definition);

    /**
     * @brief 上传相机数据到 GPU。
     * @param frameResources 帧资源。
     * @param camera 相机 UBO 数据。
     * @return 传输令牌，可用于等待上传完成。
     */
    vkcore::TransferToken uploadCameraData(const PerFrameGpuResources &frameResources,
                                           const asset::CameraUBO &camera) const;

    /**
     * @brief 上传光源数据到 GPU。
     * @param frameResources 帧资源。
     * @param lights 光源 UBO 数据。
     * @return 传输令牌，可用于等待上传完成。
     */
    vkcore::TransferToken uploadLightData(const PerFrameGpuResources &frameResources,
                                          const asset::LightUBO &lights) const;

    /**
     * @brief 开始写入指定 set 的 DescriptorSet。
     * @param frameResources 帧资源。
     * @param setIndex 目标 set 序号。
     * @return DescriptorSetWriter 便于链式写入。
     */
    vkcore::DescriptorSetWriter beginDescriptorWrite(const PerFrameGpuResources &frameResources,
                                                     uint32_t setIndex) const;

  private:
    asset::ResourceManager *m_resourceManager{nullptr};
    vkcore::TransferManager *m_transferManager{nullptr};
    vkcore::VkResourceAllocator *m_allocator{nullptr};
    vkcore::VkContext *m_context{nullptr};
};

} // namespace renderer
