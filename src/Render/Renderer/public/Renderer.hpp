#pragma once

#include "EngineServices.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "RenderPasses.hpp"
#include "RenderResources.hpp"

#include "MaterialManager.hpp"
#include "ResourceManager.hpp"
#include "Scene.hpp"

#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace renderer
{
struct RendererConfig
{
    RendererGlobalResources globalResources;          // 初始化要预加载哪些 mesh/texture/shader
    FrameResourceDefinition frameDefinition;          // per-frame UBO 定义（camera/light + framesInFlight）
    RenderPassSequence renderPasses;                  // 渲染 pass 列表
    std::string swapchainAttachmentName{"Swapchain"}; // 在 RenderAttachment 里约定的名字
};

/**
 * @brief 每个 pass 绘制时，提供给回调的上下文数据
 */
struct PassDrawContext
{
    vk::CommandBuffer cmd;
    uint32_t frameIndex;

    // per-frame 资源（包含 camera/light buffer + descriptorSets）
    PerFrameGpuResources &frameResources;

    // 全局服务
    asset::Scene &scene;
    asset::ResourceManager &resourceManager;
    asset::MaterialManager *materialManager; // 允许为空
};

using PassDrawCallback = std::function<void(const RenderPassDefinition &pass, const PassDrawContext &ctx)>;

class Renderer
{
  public:
    explicit Renderer(EngineServices &services, const RendererConfig &config);
    ~Renderer() = default;

    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    /// 交换链重建 / 尺寸变化
    void onResize(vk::Extent2D newExtent);

    /**
     * @brief 录制一帧渲染命令，内部会：
     *  1) 从 Scene 构建 CameraUBO / LightUBO 并上传
     *  2) 按 RenderPassSequence 依次执行各 pass
     */
    void recordFrame(vk::CommandBuffer cmd, uint32_t frameIndex);

    /// 为指定 passName 注册绘制回调，不注册则该 pass 不画（只做 attachment 准备）
    void registerPassCallback(const std::string &passName, PassDrawCallback cb);

    const RendererConfig &getConfig() const noexcept
    {
        return m_config;
    }

  private:
    struct PassRuntime
    {
        const RenderPassDefinition *definition{nullptr};
        vk::Pipeline pipeline{VK_NULL_HANDLE};
    };

    void initializeServices(EngineServices &services);
    void initializeGlobalResources();
    void initializeFrameResources();
    void initializePassRuntime();

    void recordPass(const PassRuntime &passRt, vk::CommandBuffer cmd, uint32_t frameIndex);

    vk::RenderingAttachmentInfo makeColorAttachmentInfo(const RenderAttachment &attachment, uint32_t frameIndex) const;
    std::optional<vk::RenderingAttachmentInfo> makeDepthAttachmentInfo(const RenderAttachment &attachment,
                                                                       uint32_t frameIndex) const;

  private:
    RendererConfig m_config;

    // —— 来自 EngineServices 的服务 —— //
    vkcore::VkContext *m_context{nullptr};
    vkcore::VkResourceAllocator *m_allocator{nullptr};
    vkcore::TransferManager *m_transfer{nullptr};
    asset::ResourceManager *m_resourceManager{nullptr};
    asset::MaterialManager *m_materialManager{nullptr};
    asset::Scene *m_scene{nullptr};

    // —— 高层辅助服务 —— //
    std::unique_ptr<RendererResourceService> m_resourceService;
    std::unique_ptr<GraphicsPipelineLibrary> m_pipelineLibrary;

    // 每帧 UBO + descriptor
    std::vector<PerFrameGpuResources> m_frameResources;
    // 每个 pass 的 pipeline 等运行时信息
    std::vector<PassRuntime> m_passRuntimes;

    // passName -> 回调
    std::unordered_map<std::string, PassDrawCallback> m_passCallbacks;

    vk::Extent2D m_renderExtent{0, 0};
};

} // namespace renderer