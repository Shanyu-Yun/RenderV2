#pragma once

#include "Render/Asset/ResourceManager/public/ResourceManager.hpp"
#include "Render/Renderer/public/RenderPasses.hpp"
#include "Render/VkCore/public/VkContext.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace renderer
{

/**
 * @brief 图形管线构建器，封装默认的动态渲染配置。
 *
 * 提供基于着色器前缀、RenderPassDefinition 输出格式自动生成管线布局
 * 与 GraphicsPipeline 的便捷接口，可按需覆盖光栅化、深度与混合状态。
 */
class GraphicsPipelineBuilder
{
  public:
    GraphicsPipelineBuilder(vkcore::VkContext &context, asset::ResourceManager &resourceManager);

    GraphicsPipelineBuilder &setShaderPrefix(std::string prefix);
    GraphicsPipelineBuilder &setPipelineLayout(vk::PipelineLayout layout);
    GraphicsPipelineBuilder &setRenderTargets(const RenderPassDefinition &pass);
    GraphicsPipelineBuilder &setRenderTargets(std::vector<vk::Format> colorFormats,
                                              std::optional<vk::Format> depthFormat = std::nullopt);
    GraphicsPipelineBuilder &setRasterization(vk::PolygonMode polygonMode, vk::CullModeFlags cullMode,
                                              vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise);
    GraphicsPipelineBuilder &setDepthState(bool depthTestEnable, bool depthWriteEnable,
                                           vk::CompareOp compareOp = vk::CompareOp::eLess);
    GraphicsPipelineBuilder &setColorBlend(bool enableBlend);

    /// 构建图形管线并返回句柄（生命周期由调用者负责）。
    vk::UniquePipeline build();

    /// 使用当前布局配置单独创建 PipelineLayout，便于复用。
    vk::UniquePipelineLayout buildPipelineLayout() const;

  private:
    std::vector<vk::PipelineShaderStageCreateInfo> buildShaderStages() const;
    vk::PipelineVertexInputStateCreateInfo buildVertexInputState();
    vk::PipelineInputAssemblyStateCreateInfo buildInputAssemblyState() const;
    vk::PipelineViewportStateCreateInfo buildViewportState() const;
    vk::PipelineRasterizationStateCreateInfo buildRasterizationState() const;
    vk::PipelineMultisampleStateCreateInfo buildMultisampleState() const;
    vk::PipelineDepthStencilStateCreateInfo buildDepthStencilState() const;
    vk::PipelineColorBlendStateCreateInfo buildColorBlendState(std::vector<vk::PipelineColorBlendAttachmentState> &)
        const;
    std::vector<vk::DynamicState> buildDynamicStates() const;
    vk::PipelineLayout createPipelineLayoutInternal(const std::vector<vk::PushConstantRange> &pushConstants) const;

    vkcore::VkContext *m_context{nullptr};
    asset::ResourceManager *m_resourceManager{nullptr};

    std::string m_shaderPrefix;
    vk::PipelineLayout m_pipelineLayout{};
    std::vector<vk::Format> m_colorFormats;
    std::optional<vk::Format> m_depthFormat;

    vk::PolygonMode m_polygonMode{vk::PolygonMode::eFill};
    vk::CullModeFlags m_cullMode{vk::CullModeFlagBits::eBack};
    vk::FrontFace m_frontFace{vk::FrontFace::eCounterClockwise};

    bool m_enableDepthTest{true};
    bool m_enableDepthWrite{true};
    vk::CompareOp m_depthCompareOp{vk::CompareOp::eLess};

    bool m_enableBlend{false};
};

/**
 * @brief 管线库，提供默认管线/布局的获取与缓存。
 */
class GraphicsPipelineLibrary
{
  public:
    GraphicsPipelineLibrary(vkcore::VkContext &context, asset::ResourceManager &resourceManager);

    /// 获取（或创建并缓存）着色器前缀对应的 PipelineLayout。
    vk::PipelineLayout getOrCreateLayout(const std::string &shaderPrefix);

    /// 基于通道输出格式获取或构建默认的 GraphicsPipeline。
    vk::Pipeline getOrCreateDefaultPipeline(const std::string &shaderPrefix, const RenderPassDefinition &pass);

  private:
    std::string makePipelineKey(const std::string &shaderPrefix, const std::vector<vk::Format> &colorFormats,
                                std::optional<vk::Format> depthFormat) const;

    vkcore::VkContext *m_context{nullptr};
    asset::ResourceManager *m_resourceManager{nullptr};

    std::unordered_map<std::string, vk::UniquePipelineLayout> m_layoutCache;
    std::unordered_map<std::string, vk::UniquePipeline> m_pipelineCache;
};

} // namespace renderer

