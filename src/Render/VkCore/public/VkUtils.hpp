/**
 * @file VkUtils.hpp
 * @author Summer
 * @brief Vulkan辅助工具函数集合
 *
 * 该文件提供了常用的Vulkan辅助函数，包括：
 * - 格式转换与查询
 * - 命令缓冲区辅助
 * - 图像布局转换
 * - 调试工具
 * - 同步辅助
 *
 * @version 1.0
 * @date 2025-11-21
 */

#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace vkcore
{
class VkContext; // 前向声明

/**
 * @namespace VkUtils
 * @brief Vulkan辅助工具函数命名空间
 */
namespace VkUtils
{

// ==================== 格式相关 ====================

/**
 * @brief 查找支持的深度格式
 * @param physicalDevice 物理设备
 * @param candidates 候选格式列表
 * @param tiling 平铺模式
 * @param features 所需特性
 * @return vk::Format 找到的格式
 */
vk::Format findSupportedFormat(vk::PhysicalDevice physicalDevice, const std::vector<vk::Format> &candidates,
                               vk::ImageTiling tiling, vk::FormatFeatureFlags features);

/**
 * @brief 查找深度格式
 * @param physicalDevice 物理设备
 * @return vk::Format 深度格式
 */
vk::Format findDepthFormat(vk::PhysicalDevice physicalDevice);

/**
 * @brief 检查格式是否有深度分量
 * @param format 格式
 * @return bool 是否有深度分量
 */
bool hasDepthComponent(vk::Format format);

/**
 * @brief 检查格式是否有模板分量
 * @param format 格式
 * @return bool 是否有模板分量
 */
bool hasStencilComponent(vk::Format format);

/**
 * @brief 获取格式的字节大小
 * @param format 格式
 * @return uint32_t 字节大小
 */
uint32_t getFormatSize(vk::Format format);

// ==================== 图像布局转换 ====================

/**
 * @brief 图像布局转换辅助结构
 */
struct ImageLayoutTransition
{
    vk::Image image;
    vk::ImageLayout oldLayout;
    vk::ImageLayout newLayout;
    vk::ImageAspectFlags aspectMask;
    uint32_t baseMipLevel = 0;
    uint32_t levelCount = 1;
    uint32_t baseArrayLayer = 0;
    uint32_t layerCount = 1;
};

/**
 * @brief 执行图像布局转换
 * @param commandBuffer 命令缓冲区
 * @param transition 转换参数
 */
void transitionImageLayout(vk::CommandBuffer commandBuffer, const ImageLayoutTransition &transition);

/**
 * @brief 获取访问掩码和管线阶段（用于布局转换）
 * @param layout 图像布局
 * @param accessMask 输出：访问掩码
 * @param stage 输出：管线阶段
 */
void getLayoutAccessMaskAndStage(vk::ImageLayout layout, vk::AccessFlags &accessMask, vk::PipelineStageFlags &stage);

// ==================== 命令缓冲区辅助 ====================

/**
 * @brief 创建一次性命令缓冲区
 * @param device 逻辑设备
 * @param commandPool 命令池
 * @return vk::CommandBuffer 命令缓冲区
 */
vk::CommandBuffer beginSingleTimeCommands(vk::Device device, vk::CommandPool commandPool);

/**
 * @brief 结束并提交一次性命令缓冲区
 * @param device 逻辑设备
 * @param commandPool 命令池
 * @param commandBuffer 命令缓冲区
 * @param queue 提交队列
 */
void endSingleTimeCommands(vk::Device device, vk::CommandPool commandPool, vk::CommandBuffer commandBuffer,
                           vk::Queue queue);

// ==================== 内存相关 ====================

/**
 * @brief 查找合适的内存类型
 * @param physicalDevice 物理设备
 * @param typeFilter 类型过滤器
 * @param properties 所需属性
 * @return uint32_t 内存类型索引
 */
uint32_t findMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties);

// ==================== 调试与日志 ====================

/**
 * @brief 获取Vulkan结果的字符串描述
 * @param result Vulkan结果
 * @return std::string 描述字符串
 */
std::string vkResultToString(vk::Result result);

/**
 * @brief 设置对象调试名称
 * @param context Vulkan上下文
 * @param device 逻辑设备
 * @param objectHandle 对象句柄
 * @param objectType 对象类型
 * @param name 名称
 */
void setDebugObjectName(VkContext &context, vk::Device device, uint64_t objectHandle, vk::ObjectType objectType,
                        const std::string &name);

/**
 * @brief 插入调试标签（用于命令缓冲区）
 * @param context Vulkan上下文
 * @param commandBuffer 命令缓冲区
 * @param name 标签名称
 * @param color 标签颜色（RGBA，0-1范围）
 */
void insertDebugLabel(VkContext &context, vk::CommandBuffer commandBuffer, const std::string &name,
                      const float color[4]);

/**
 * @brief 开始调试标签区域
 * @param context Vulkan上下文
 * @param commandBuffer 命令缓冲区
 * @param name 区域名称
 * @param color 颜色（RGBA，0-1范围）
 */
void beginDebugLabel(VkContext &context, vk::CommandBuffer commandBuffer, const std::string &name,
                     const float color[4]);

/**
 * @brief 结束调试标签区域
 * @param context Vulkan上下文
 * @param commandBuffer 命令缓冲区
 */
void endDebugLabel(VkContext &context, vk::CommandBuffer commandBuffer);

// ==================== 图像操作 ====================

/**
 * @brief 复制缓冲区到图像
 * @param commandBuffer 命令缓冲区
 * @param buffer 源缓冲区
 * @param image 目标图像
 * @param width 宽度
 * @param height 高度
 * @param layerCount 层数
 */
void copyBufferToImage(vk::CommandBuffer commandBuffer, vk::Buffer buffer, vk::Image image, uint32_t width,
                       uint32_t height, uint32_t layerCount = 1);

/**
 * @brief 复制图像到缓冲区
 * @param commandBuffer 命令缓冲区
 * @param image 源图像
 * @param buffer 目标缓冲区
 * @param width 宽度
 * @param height 高度
 * @param layerCount 层数
 */
void copyImageToBuffer(vk::CommandBuffer commandBuffer, vk::Image image, vk::Buffer buffer, uint32_t width,
                       uint32_t height, uint32_t layerCount = 1);

/**
 * @brief 生成Mipmap
 * @param commandBuffer 命令缓冲区
 * @param image 图像
 * @param format 格式
 * @param width 宽度
 * @param height 高度
 * @param mipLevels Mip层数
 */
void generateMipmaps(vk::CommandBuffer commandBuffer, vk::Image image, vk::Format format, int32_t width, int32_t height,
                     uint32_t mipLevels);

// ==================== 缓冲区操作 ====================

/**
 * @brief 复制缓冲区
 * @param commandBuffer 命令缓冲区
 * @param srcBuffer 源缓冲区
 * @param dstBuffer 目标缓冲区
 * @param size 大小
 */
void copyBuffer(vk::CommandBuffer commandBuffer, vk::Buffer srcBuffer, vk::Buffer dstBuffer, vk::DeviceSize size);

// ==================== Shader编译辅助 ====================

/**
 * @brief 从文件加载SPIR-V着色器
 * @param filename 文件名
 * @return std::vector<uint32_t> SPIR-V字节码
 */
std::vector<uint32_t> loadSPIRV(const std::string &filename);

/**
 * @brief 从GLSL源码编译SPIR-V（需要shaderc）
 * @param source GLSL源码
 * @param shaderType 着色器类型
 * @param filename 文件名（用于错误信息）
 * @return std::vector<uint32_t> SPIR-V字节码
 */
std::vector<uint32_t> compileGLSLToSPIRV(const std::string &source, vk::ShaderStageFlagBits shaderType,
                                         const std::string &filename = "shader");

// ==================== 验证与错误检查 ====================

/**
 * @brief 检查Vulkan结果，如果失败则抛出异常
 * @param result Vulkan结果
 * @param message 错误消息
 */
void checkVkResult(vk::Result result, const std::string &message);

/**
 * @brief 验证层检查
 * @param layerName 层名称
 * @return bool 是否支持
 */
bool isValidationLayerSupported(const std::string &layerName);

/**
 * @brief 检查设备扩展支持
 * @param physicalDevice 物理设备
 * @param extensionName 扩展名称
 * @return bool 是否支持
 */
bool isDeviceExtensionSupported(vk::PhysicalDevice physicalDevice, const std::string &extensionName);

// ==================== 对齐辅助 ====================

/**
 * @brief 计算对齐后的大小
 * @param size 原始大小
 * @param alignment 对齐要求
 * @return size_t 对齐后的大小
 */
inline size_t alignedSize(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

/**
 * @brief 获取uniform buffer的对齐要求
 * @param physicalDevice 物理设备
 * @return size_t 对齐大小
 */
size_t getUniformBufferAlignment(vk::PhysicalDevice physicalDevice);

/**
 * @brief 获取storage buffer的对齐要求
 * @param physicalDevice 物理设备
 * @return size_t 对齐大小
 */
size_t getStorageBufferAlignment(vk::PhysicalDevice physicalDevice);

} // namespace VkUtils

} // namespace vkcore
