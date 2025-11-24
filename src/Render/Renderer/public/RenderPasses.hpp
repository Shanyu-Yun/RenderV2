#pragma once

#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace renderer
{

/**
 * @brief 渲染通道支持的附件类型。
 */
enum class AttachmentType
{
    Color,        ///< 颜色附件，将作为渲染输出或需要被清除。
    DepthStencil, ///< 深度/模板附件，用于深度测试或写入。
};

/**
 * @brief 对单个附件的访问与动态渲染参数描述。
 *
 * 该结构体聚合了动态渲染需要的格式、采样、Load/Store 策略以及清除值，
 * 并以资源名标记它对应的 ResourceManager 图像或交换链目标。
 */
struct RenderAttachment
{
    AttachmentType type{AttachmentType::Color}; ///< 附件类别。
    std::string resourceName;                   ///< 资源名称，交换链可使用诸如"Swapchain"的约定名称。

    vk::Format format{vk::Format::eUndefined};                     ///< Vulkan 图像格式。
    vk::SampleCountFlagBits samples{vk::SampleCountFlagBits::e1};  ///< 采样数。
    vk::AttachmentLoadOp loadOp{vk::AttachmentLoadOp::eClear};     ///< 初始加载策略。
    vk::AttachmentStoreOp storeOp{vk::AttachmentStoreOp::eStore};  ///< 结束存储策略。
    std::optional<vk::ClearValue> clearValue{};                    ///< 可选清除值，当 loadOp 为 Clear 时使用。
};

/**
 * @brief 渲染通道的输入输出声明。
 *
 * 通过该结构可以明确描述本通道读取/写入的 GPU 资源，为多阶段渲染提供
 * 依赖分析与 Barrier 插入所需的元信息。
 */
struct RenderPassIO
{
    /// 将以颜色附件形式写入的图像。
    std::vector<RenderAttachment> colorOutputs;
    /// 可选的深度/模板附件。
    std::optional<RenderAttachment> depthStencilOutput;

    /// 只读采样纹理名称列表（例如前序 Pass 的输出）。
    std::vector<std::string> sampledImages;
    /// 以 Storage 形式读写的图像名称列表。
    std::vector<std::string> storageImages;
    /// 只读 Uniform/Storage 缓冲名称列表。
    std::vector<std::string> bufferInputs;
    /// 可能被写入的 Storage 缓冲名称列表。
    std::vector<std::string> bufferOutputs;
};

/**
 * @brief 单个渲染通道的完整定义。
 *
 * 包含 shader 前缀、资源读写声明以及用于动态渲染的输出区域信息。
 */
struct RenderPassDefinition
{
    std::string name;          ///< 渲染通道名称，需在图中唯一。
    std::string shaderPrefix;  ///< 使用的着色器前缀，用于 Shader/Descriptor 查询。
    RenderPassIO resources;    ///< 本通道的输入输出资源描述。

    /// 期望的渲染区域尺寸，通常对应目标附件的尺寸。
    vk::Extent2D renderExtent{0, 0};
};

/**
 * @brief 渲染通道列表的简易管理器。
 *
 * 该容器保证通道名称唯一，并提供按名称查询与顺序遍历能力，便于在渲染
 * 调度阶段按顺序生成动态渲染命令。
 */
class RenderPassSequence
{
  public:
    /// 新增一个渲染通道定义，若名称重复则抛出异常。
    void addPass(RenderPassDefinition pass);

    /// 按插入顺序访问全部通道。
    const std::vector<RenderPassDefinition> &getPasses() const noexcept
    {
        return m_passes;
    }

    /// 按名称查找通道，未找到返回 nullptr。
    const RenderPassDefinition *findPass(const std::string &name) const noexcept;

  private:
    std::vector<RenderPassDefinition> m_passes;
};

} // namespace renderer

