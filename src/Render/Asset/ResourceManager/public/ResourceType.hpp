#pragma once
#include "VkResource.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vulkan/vulkan.hpp>

namespace asset
{

struct ShaderModule
{
    vk::Device device;             ///< Vulkan 逻辑设备句柄
    vk::ShaderModule shaderModule; ///< 着色器模块句柄
    vk::ShaderStageFlagBits stage; ///< 着色器阶段标志（顶点、片段等）

    /**
     * @brief 通过 SPIR-V 字节码创建 ShaderModule
     */
    ShaderModule(vk::Device dev, const std::vector<uint32_t> &spirvCode, vk::ShaderStageFlagBits st)
        : device(dev), stage(st)
    {
        vk::ShaderModuleCreateInfo createInfo{};
        createInfo.codeSize = spirvCode.size() * sizeof(uint32_t);
        createInfo.pCode = spirvCode.data();

        shaderModule = device.createShaderModule(createInfo);
    }

    /**
     * @brief 析构函数，自动销毁着色器模块。
     */
    ~ShaderModule()
    {
        if (shaderModule != vk::ShaderModule())
        {
            device.destroyShaderModule(shaderModule);
            shaderModule = nullptr;
        }
    }

    // 禁用拷贝，启用移动
    ShaderModule(const ShaderModule &) = delete;
    ShaderModule &operator=(const ShaderModule &) = delete;

    /**
     * @brief 移动构造函数。
     * @param other 被移动的对象
     */
    ShaderModule(ShaderModule &&other) noexcept
        : device(other.device), shaderModule(other.shaderModule), stage(other.stage)
    {
        other.shaderModule = vk::ShaderModule{};
        other.device = vk::Device{};
    }

    /**
     * @brief 移动赋值运算符。
     * @param other 被移动的对象
     * @return ShaderModule& 自身引用
     */
    ShaderModule &operator=(ShaderModule &&other) noexcept
    {
        if (this != &other)
        {
            if (shaderModule)
            {
                device.destroyShaderModule(shaderModule);
            }
            device = other.device;
            shaderModule = other.shaderModule;
            stage = other.stage;
            other.shaderModule = vk::ShaderModule{};
            other.device = vk::Device{};
        }
        return *this;
    }
};

struct shaderProgram
{
    std::shared_ptr<ShaderModule> vertexShader;
    std::shared_ptr<ShaderModule> fragmentShader;
    std::shared_ptr<ShaderModule> computeShader;
    inline bool isValid() const
    {
        return vertexShader != nullptr || fragmentShader != nullptr || computeShader != nullptr;
    }
    inline bool hasComputeShader() const
    {
        return computeShader != nullptr;
    }
};

/**
 * @struct Vertex
 * @brief 顶点的标准布局
 * @details 成员按大小降序排列以优化内存对齐
 */
struct Vertex
{
    glm::vec4 color;    // 16 bytes
    glm::vec3 position; // 12 bytes
    glm::vec3 normal;   // 12 bytes
    glm::vec2 texCoord; // 8 bytes

    static vk::VertexInputBindingDescription getBindingDescription()
    {
        vk::VertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = vk::VertexInputRate::eVertex;
        return bindingDescription;
    }

    static std::array<vk::VertexInputAttributeDescription, 4> getAttributeDescriptions()
    {
        std::array<vk::VertexInputAttributeDescription, 4> attributeDescriptions{};

        // color: vec4
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = vk::Format::eR32G32B32A32Sfloat;
        attributeDescriptions[0].offset = offsetof(Vertex, color);

        // position: vec3
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[1].offset = offsetof(Vertex, position);

        // normal: vec3
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = vk::Format::eR32G32B32Sfloat;
        attributeDescriptions[2].offset = offsetof(Vertex, normal);

        // texCoord: vec2
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = vk::Format::eR32G32Sfloat;
        attributeDescriptions[3].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }
};

/**
 * @enum AlphaMode
 * @brief Alpha 混合模式
 */
enum class AlphaMode
{
    Opaque, ///< 不透明（忽略 alpha 值）
    Mask,   ///< Alpha 测试（alphaCutoff 阈值）
    Blend   ///< Alpha 混合（透明）
};

} // namespace asset