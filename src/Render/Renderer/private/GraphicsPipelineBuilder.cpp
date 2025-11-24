#include "Render/Renderer/public/GraphicsPipelineBuilder.hpp"

#include "Render/Asset/ResourceManager/public/ResourceType.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace renderer
{

namespace
{
/// 持有内部创建的 PipelineLayout，保证其在管线生命周期内有效。
std::vector<vk::UniquePipelineLayout> g_ownedPipelineLayouts;

std::vector<vk::Format> collectColorFormats(const RenderPassDefinition &pass)
{
    std::vector<vk::Format> formats;
    formats.reserve(pass.resources.colorOutputs.size());
    for (const auto &attachment : pass.resources.colorOutputs)
    {
        formats.push_back(attachment.format);
    }
    return formats;
}

std::optional<vk::Format> collectDepthFormat(const RenderPassDefinition &pass)
{
    if (pass.resources.depthStencilOutput)
    {
        return pass.resources.depthStencilOutput->format;
    }
    return std::nullopt;
}

std::vector<vk::DescriptorSetLayout> collectLayouts(
    const std::vector<std::shared_ptr<const vkcore::DescriptorSetSchema>> &schemas)
{
    std::vector<vk::DescriptorSetLayout> layouts;
    layouts.reserve(schemas.size());
    for (auto &schema : schemas)
    {
        if (schema)
        {
            layouts.push_back(schema->getLayout());
        }
    }
    return layouts;
}

} // namespace

GraphicsPipelineBuilder::GraphicsPipelineBuilder(vkcore::VkContext &context, asset::ResourceManager &resourceManager)
    : m_context(&context), m_resourceManager(&resourceManager)
{
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setShaderPrefix(std::string prefix)
{
    m_shaderPrefix = std::move(prefix);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setPipelineLayout(vk::PipelineLayout layout)
{
    m_pipelineLayout = layout;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setRenderTargets(const RenderPassDefinition &pass)
{
    m_colorFormats = collectColorFormats(pass);
    m_depthFormat = collectDepthFormat(pass);
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setRenderTargets(std::vector<vk::Format> colorFormats,
                                                                   std::optional<vk::Format> depthFormat)
{
    m_colorFormats = std::move(colorFormats);
    m_depthFormat = depthFormat;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setRasterization(vk::PolygonMode polygonMode,
                                                                   vk::CullModeFlags cullMode, vk::FrontFace frontFace)
{
    m_polygonMode = polygonMode;
    m_cullMode = cullMode;
    m_frontFace = frontFace;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setDepthState(bool depthTestEnable, bool depthWriteEnable,
                                                                vk::CompareOp compareOp)
{
    m_enableDepthTest = depthTestEnable;
    m_enableDepthWrite = depthWriteEnable;
    m_depthCompareOp = compareOp;
    return *this;
}

GraphicsPipelineBuilder &GraphicsPipelineBuilder::setColorBlend(bool enableBlend)
{
    m_enableBlend = enableBlend;
    return *this;
}

vk::UniquePipeline GraphicsPipelineBuilder::build()
{
    if (m_shaderPrefix.empty())
    {
        throw std::runtime_error("GraphicsPipelineBuilder requires a shader prefix");
    }
    if (m_colorFormats.empty())
    {
        throw std::runtime_error("GraphicsPipelineBuilder requires at least one color format");
    }

    auto shaderStages = buildShaderStages();
    if (shaderStages.empty())
    {
        throw std::runtime_error("Shader program for prefix " + m_shaderPrefix + " is invalid");
    }

    auto vertexInput = buildVertexInputState();
    auto inputAssembly = buildInputAssemblyState();
    auto viewportState = buildViewportState();
    auto rasterizationState = buildRasterizationState();
    auto multisampleState = buildMultisampleState();
    auto depthStencilState = buildDepthStencilState();
    std::vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
    auto colorBlendState = buildColorBlendState(colorBlendAttachments);
    auto dynamicStates = buildDynamicStates();

    vk::UniquePipelineLayout ownedLayout;
    vk::PipelineLayout pipelineLayout = m_pipelineLayout;
    if (!pipelineLayout)
    {
        ownedLayout = buildPipelineLayout();
        pipelineLayout = ownedLayout.get();
        g_ownedPipelineLayouts.emplace_back(std::move(ownedLayout));
    }

    vk::PipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.setColorAttachmentFormats(m_colorFormats);
    if (m_depthFormat)
    {
        renderingInfo.setDepthAttachmentFormat(*m_depthFormat);
    }

    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.setPNext(&renderingInfo);
    pipelineInfo.setStages(shaderStages);
    pipelineInfo.setPVertexInputState(&vertexInput);
    pipelineInfo.setPInputAssemblyState(&inputAssembly);
    pipelineInfo.setPViewportState(&viewportState);
    pipelineInfo.setPRasterizationState(&rasterizationState);
    pipelineInfo.setPMultisampleState(&multisampleState);
    pipelineInfo.setPDepthStencilState(&depthStencilState);
    pipelineInfo.setPColorBlendState(&colorBlendState);
    pipelineInfo.setLayout(pipelineLayout);
    pipelineInfo.setDynamicState({static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data()});

    return m_context->getDevice().createGraphicsPipelineUnique({}, pipelineInfo);
}

vk::UniquePipelineLayout GraphicsPipelineBuilder::buildPipelineLayout() const
{
    return vk::UniquePipelineLayout(createPipelineLayoutInternal({}), m_context->getDevice());
}

std::vector<vk::PipelineShaderStageCreateInfo> GraphicsPipelineBuilder::buildShaderStages() const
{
    auto program = m_resourceManager->getShaderprogram(m_shaderPrefix);
    std::vector<vk::PipelineShaderStageCreateInfo> stages;
    if (program.vertexShader)
    {
        stages.emplace_back(vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eVertex,
                            program.vertexShader->shaderModule, "main");
    }
    if (program.fragmentShader)
    {
        stages.emplace_back(vk::PipelineShaderStageCreateFlags{}, vk::ShaderStageFlagBits::eFragment,
                            program.fragmentShader->shaderModule, "main");
    }
    return stages;
}

vk::PipelineVertexInputStateCreateInfo GraphicsPipelineBuilder::buildVertexInputState()
{
    static const std::array<vk::VertexInputBindingDescription, 1> bindings = {
        vk::VertexInputBindingDescription{0, sizeof(asset::Vertex), vk::VertexInputRate::eVertex},
    };

    static const std::array<vk::VertexInputAttributeDescription, 4> attributes = {
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat,
                                            static_cast<uint32_t>(offsetof(asset::Vertex, position))},
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat,
                                            static_cast<uint32_t>(offsetof(asset::Vertex, normal))},
        vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32Sfloat,
                                            static_cast<uint32_t>(offsetof(asset::Vertex, texCoord))},
        vk::VertexInputAttributeDescription{3, 0, vk::Format::eR32G32B32A32Sfloat,
                                            static_cast<uint32_t>(offsetof(asset::Vertex, color))},
    };

    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.setVertexBindingDescriptions(bindings);
    vertexInputInfo.setVertexAttributeDescriptions(attributes);
    return vertexInputInfo;
}

vk::PipelineInputAssemblyStateCreateInfo GraphicsPipelineBuilder::buildInputAssemblyState() const
{
    return {vk::PipelineInputAssemblyStateCreateFlags{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
}

vk::PipelineViewportStateCreateInfo GraphicsPipelineBuilder::buildViewportState() const
{
    return {vk::PipelineViewportStateCreateFlags{}, 1, nullptr, 1, nullptr};
}

vk::PipelineRasterizationStateCreateInfo GraphicsPipelineBuilder::buildRasterizationState() const
{
    vk::PipelineRasterizationStateCreateInfo raster{};
    raster.setCullMode(m_cullMode);
    raster.setPolygonMode(m_polygonMode);
    raster.setFrontFace(m_frontFace);
    raster.setLineWidth(1.0f);
    raster.setDepthClampEnable(VK_FALSE);
    raster.setRasterizerDiscardEnable(VK_FALSE);
    return raster;
}

vk::PipelineMultisampleStateCreateInfo GraphicsPipelineBuilder::buildMultisampleState() const
{
    vk::PipelineMultisampleStateCreateInfo multisample{};
    multisample.setRasterizationSamples(vk::SampleCountFlagBits::e1);
    multisample.setSampleShadingEnable(VK_FALSE);
    return multisample;
}

vk::PipelineDepthStencilStateCreateInfo GraphicsPipelineBuilder::buildDepthStencilState() const
{
    vk::PipelineDepthStencilStateCreateInfo depth{};
    depth.setDepthTestEnable(m_enableDepthTest);
    depth.setDepthWriteEnable(m_enableDepthWrite);
    depth.setDepthCompareOp(m_depthCompareOp);
    depth.setDepthBoundsTestEnable(VK_FALSE);
    depth.setStencilTestEnable(VK_FALSE);
    return depth;
}

vk::PipelineColorBlendStateCreateInfo
GraphicsPipelineBuilder::buildColorBlendState(std::vector<vk::PipelineColorBlendAttachmentState> &attachments) const
{
    attachments.clear();
    attachments.reserve(m_colorFormats.size());

    vk::PipelineColorBlendAttachmentState defaultState{};
    defaultState.setBlendEnable(m_enableBlend);
    if (m_enableBlend)
    {
        defaultState.setColorBlendOp(vk::BlendOp::eAdd);
        defaultState.setAlphaBlendOp(vk::BlendOp::eAdd);
        defaultState.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha);
        defaultState.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
        defaultState.setSrcAlphaBlendFactor(vk::BlendFactor::eOne);
        defaultState.setDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
    }
    defaultState.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    for (size_t i = 0; i < m_colorFormats.size(); ++i)
    {
        attachments.push_back(defaultState);
    }

    vk::PipelineColorBlendStateCreateInfo blend{};
    blend.setLogicOpEnable(VK_FALSE);
    blend.setAttachments(attachments);
    return blend;
}

std::vector<vk::DynamicState> GraphicsPipelineBuilder::buildDynamicStates() const
{
    return {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
}

vk::PipelineLayout GraphicsPipelineBuilder::createPipelineLayoutInternal(
    const std::vector<vk::PushConstantRange> &pushConstants) const
{
    auto schemas = m_resourceManager->getShaderDescriptorSchemas(m_shaderPrefix);
    auto layouts = collectLayouts(schemas);

    vk::PipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.setSetLayouts(layouts);
    layoutInfo.setPushConstantRanges(pushConstants);

    return m_context->getDevice().createPipelineLayout(layoutInfo);
}

GraphicsPipelineLibrary::GraphicsPipelineLibrary(vkcore::VkContext &context, asset::ResourceManager &resourceManager)
    : m_context(&context), m_resourceManager(&resourceManager)
{
}

vk::PipelineLayout GraphicsPipelineLibrary::getOrCreateLayout(const std::string &shaderPrefix)
{
    auto iter = m_layoutCache.find(shaderPrefix);
    if (iter != m_layoutCache.end())
    {
        return iter->second.get();
    }

    GraphicsPipelineBuilder builder(*m_context, *m_resourceManager);
    builder.setShaderPrefix(shaderPrefix);
    auto layout = builder.buildPipelineLayout();
    auto handle = layout.get();
    m_layoutCache.emplace(shaderPrefix, std::move(layout));
    return handle;
}

vk::Pipeline GraphicsPipelineLibrary::getOrCreateDefaultPipeline(const std::string &shaderPrefix,
                                                                 const RenderPassDefinition &pass)
{
    auto key = makePipelineKey(shaderPrefix, collectColorFormats(pass), collectDepthFormat(pass));
    auto iter = m_pipelineCache.find(key);
    if (iter != m_pipelineCache.end())
    {
        return iter->second.get();
    }

    GraphicsPipelineBuilder builder(*m_context, *m_resourceManager);
    auto layout = getOrCreateLayout(shaderPrefix);
    builder.setShaderPrefix(shaderPrefix).setPipelineLayout(layout).setRenderTargets(pass);

    auto pipeline = builder.build();
    auto handle = pipeline.get();
    m_pipelineCache.emplace(key, std::move(pipeline));
    return handle;
}

std::string GraphicsPipelineLibrary::makePipelineKey(const std::string &shaderPrefix,
                                                     const std::vector<vk::Format> &colorFormats,
                                                     std::optional<vk::Format> depthFormat) const
{
    std::string key = shaderPrefix + "|C";
    for (auto fmt : colorFormats)
    {
        key += ":" + std::to_string(static_cast<int>(fmt));
    }
    key += "|D:";
    key += depthFormat ? std::to_string(static_cast<int>(*depthFormat)) : std::string("None");
    return key;
}

} // namespace renderer

