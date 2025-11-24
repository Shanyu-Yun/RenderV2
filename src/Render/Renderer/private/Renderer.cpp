#include "Renderer.hpp"

#include "Scene.hpp"
#include "VkContext.hpp"

namespace renderer
{

Renderer::Renderer(EngineServices &services, const RendererConfig &config) : m_config(config)
{
    initializeServices(services);
    initializeGlobalResources();
    initializeFrameResources();
    initializePassRuntime();
}

void Renderer::onResize(vk::Extent2D newExtent)
{
    if (!m_context)
    {
        return;
    }

    m_renderExtent = newExtent;
    m_context->recreateSwapchain(newExtent.width, newExtent.height);
    initializePassRuntime();
}

void Renderer::recordFrame(vk::CommandBuffer cmd, uint32_t frameIndex)
{
    if (!m_scene)
    {
        return;
    }

    const auto cameraUBO = m_scene->buildActiveCameraUBO();
    const auto lightUBO = m_scene->buildLightUBO();

    auto &frameResources = m_frameResources[frameIndex % m_frameResources.size()];
    auto cameraToken = m_resourceService->uploadCameraData(frameResources, cameraUBO);
    auto lightToken = m_resourceService->uploadLightData(frameResources, lightUBO);
    cameraToken.wait();
    lightToken.wait();

    for (const auto &passRt : m_passRuntimes)
    {
        recordPass(passRt, cmd, frameIndex);
    }
}

void Renderer::registerPassCallback(const std::string &passName, PassDrawCallback cb)
{
    m_passCallbacks[passName] = std::move(cb);
}

void Renderer::initializeServices(EngineServices &services)
{
    m_context = services.tryGetService<vkcore::VkContext>();
    m_allocator = services.tryGetService<vkcore::VkResourceAllocator>();
    m_transfer = services.tryGetService<vkcore::TransferManager>();
    m_resourceManager = services.tryGetService<asset::ResourceManager>();
    m_materialManager = services.tryGetService<asset::MaterialManager>();
    m_scene = services.tryGetService<asset::Scene>();

    m_resourceService = std::make_unique<RendererResourceService>(*m_resourceManager, *m_transfer, *m_allocator, *m_context);
    m_pipelineLibrary = std::make_unique<GraphicsPipelineLibrary>(*m_context, *m_resourceManager);

    if (m_context)
    {
        m_renderExtent = m_context->getSwapchainExtent();
    }
}

void Renderer::initializeGlobalResources()
{
    if (m_resourceService)
    {
        m_resourceService->preloadGlobalResources(m_config.globalResources);
    }
}

void Renderer::initializeFrameResources()
{
    m_frameResources.clear();
    for (uint32_t i = 0; i < m_config.frameDefinition.framesInFlight; ++i)
    {
        m_frameResources.emplace_back(m_resourceService->createPerFrameResources(m_config.frameDefinition));
    }
}

void Renderer::initializePassRuntime()
{
    m_passRuntimes.clear();
    for (const auto &pass : m_config.renderPasses.getPasses())
    {
        PassRuntime runtime{};
        runtime.definition = &pass;
        runtime.pipeline = m_pipelineLibrary->getOrCreateDefaultPipeline(pass.shaderPrefix, pass);
        runtime.layout = m_pipelineLibrary->getOrCreateLayout(pass.shaderPrefix);
        m_passRuntimes.emplace_back(runtime);
    }
}

void Renderer::recordPass(const PassRuntime &passRt, vk::CommandBuffer cmd, uint32_t frameIndex)
{
    if (!passRt.definition)
    {
        return;
    }

    std::vector<vk::RenderingAttachmentInfo> colorAttachments;
    for (const auto &attachment : passRt.definition->resources.colorOutputs)
    {
        colorAttachments.push_back(makeColorAttachmentInfo(attachment, frameIndex));
    }

    auto depthAttachment = passRt.definition->resources.depthStencilOutput
                               ? makeDepthAttachmentInfo(*passRt.definition->resources.depthStencilOutput, frameIndex)
                               : std::nullopt;

    vk::RenderingInfo renderingInfo{};
    renderingInfo.setColorAttachments(colorAttachments);
    if (depthAttachment)
    {
        renderingInfo.setPDepthAttachment(&*depthAttachment);
    }

    vk::Extent2D extent = passRt.definition->renderExtent.width > 0 ? passRt.definition->renderExtent : m_renderExtent;
    renderingInfo.renderArea = vk::Rect2D{{0, 0}, extent};
    renderingInfo.layerCount = 1;

    cmd.beginRendering(renderingInfo);

    vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    cmd.setViewport(0, viewport);
    cmd.setScissor(0, vk::Rect2D{{0, 0}, extent});

    if (passRt.pipeline)
    {
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, passRt.pipeline);
    }

    PassDrawContext ctx{cmd, frameIndex % static_cast<uint32_t>(m_frameResources.size()), passRt.layout,
                        m_frameResources[frameIndex % m_frameResources.size()], *m_scene, *m_resourceManager,
                        m_materialManager};

    auto iter = m_passCallbacks.find(passRt.definition->name);
    if (iter != m_passCallbacks.end() && iter->second)
    {
        iter->second(*passRt.definition, ctx);
    }

    cmd.endRendering();
}

vk::RenderingAttachmentInfo Renderer::makeColorAttachmentInfo(const RenderAttachment &attachment, uint32_t frameIndex) const
{
    vk::RenderingAttachmentInfo info{};
    info.setImageLayout(vk::ImageLayout::eAttachmentOptimal);
    info.setLoadOp(attachment.loadOp);
    info.setStoreOp(attachment.storeOp);

    if (attachment.clearValue)
    {
        info.setClearValue(*attachment.clearValue);
    }

    if (attachment.resourceName == m_config.swapchainAttachmentName && m_context)
    {
        const auto &views = m_context->getSwapchainImageViews();
        info.setImageView(views[frameIndex % views.size()]);
    }

    return info;
}

std::optional<vk::RenderingAttachmentInfo> Renderer::makeDepthAttachmentInfo(const RenderAttachment &attachment,
                                                                             uint32_t frameIndex) const
{
    if (attachment.type != AttachmentType::DepthStencil)
    {
        return std::nullopt;
    }

    vk::RenderingAttachmentInfo info{};
    info.setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
    info.setLoadOp(attachment.loadOp);
    info.setStoreOp(attachment.storeOp);
    if (attachment.clearValue)
    {
        info.setClearValue(*attachment.clearValue);
    }
    return info;
}

} // namespace renderer
