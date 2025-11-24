/**
 * @file VkContext.hpp
 * @author Summer
 * @brief Vulkan上下文管理类，负责初始化和管理Vulkan核心对象
 *
 * 该文件提供了Vulkan应用程序的核心上下文管理，包括：
 * - Instance创建和验证层配置
 * - Physical Device选择
 * - Logical Device和队列创建
 * - Swapchain管理
 * - Surface创建和管理
 *
 * @version 1.0
 * @date 2025-11-21
 */

#pragma once

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace vkcore
{

/**
 * @struct QueueFamilyIndices
 * @brief 队列族索引结构，存储不同类型队列的索引
 */
struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily; ///< 图形队列族索引
    std::optional<uint32_t> presentFamily;  ///< 呈现队列族索引
    std::optional<uint32_t> computeFamily;  ///< 计算队列族索引
    std::optional<uint32_t> transferFamily; ///< 传输队列族索引

    /**
     * @brief 检查是否所有必需的队列族都已找到
     * @return true 如果图形和呈现队列族都已找到
     */
    bool isComplete() const
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

/**
 * @struct SwapchainSupportDetails
 * @brief Swapchain支持详情，包含capabilities、formats和present modes
 */
struct SwapchainSupportDetails
{
    vk::SurfaceCapabilitiesKHR capabilities;      ///< Surface能力
    std::vector<vk::SurfaceFormatKHR> formats;    ///< 支持的格式
    std::vector<vk::PresentModeKHR> presentModes; ///< 支持的呈现模式
};

/**
 * @struct InstanceConfig
 * @brief Instance创建配置
 */
struct InstanceConfig
{
    std::string appName = "VulkanApp";
    uint32_t appVersion = VK_MAKE_VERSION(1, 0, 0);
    std::string engineName = "QTRender_v2";
    uint32_t engineVersion = VK_MAKE_VERSION(1, 0, 0);
    uint32_t apiVersion = VK_API_VERSION_1_3;
    bool enableValidation = true;
    std::vector<const char *> instanceExtensions;
    std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};
};

/**
 * @struct DeviceConfig
 * @brief Device创建配置和特性启用
 */
struct DeviceConfig
{
    // Vulkan 1.0 特性
    vk::PhysicalDeviceFeatures features10{};

    // Vulkan 1.1 特性
    bool enableShaderDrawParameters = false;

    // Vulkan 1.2 特性
    bool enableDescriptorIndexing = true;
    bool enableBufferDeviceAddress = true;
    bool enableTimelineSemaphore = true;
    bool enableScalarBlockLayout = true;
    bool enableUniformAndStorageBuffer8BitAccess = false;
    bool enableShaderFloat16 = false;
    bool enableShaderInt8 = false;

    // Vulkan 1.3 特性
    bool enableDynamicRendering = true;
    bool enableSynchronization2 = true;
    bool enableMaintenance4 = true;

    // 扩展
    std::vector<const char *> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    // 队列优先级
    float queuePriority = 1.0f;
};

/**
 * @struct SwapchainConfig
 * @brief Swapchain创建配置
 */
struct SwapchainConfig
{
    uint32_t width = 1280;
    uint32_t height = 720;
    bool vsync = true;
    vk::PresentModeKHR preferredPresentMode = vk::PresentModeKHR::eFifo;
    vk::Format preferredFormat = vk::Format::eB8G8R8A8Unorm;
    vk::ColorSpaceKHR preferredColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    uint32_t imageCount = 3; // 0表示使用minImageCount + 1
};

/**
 * @class VkContext
 * @brief Vulkan上下文管理类，单例模式
 *
 * 该类负责初始化和管理Vulkan的核心对象，包括实例、设备、队列和交换链。
 * 提供了完整的生命周期管理和资源清理。
 */
class VkContext
{
  public:
    /**
     * @brief 获取VkContext单例
     * @return VkContext& 单例引用
     */
    static VkContext &getInstance();

    /** 禁用拷贝构造与拷贝赋值 */
    VkContext(const VkContext &) = delete;
    VkContext &operator=(const VkContext &) = delete;

    /**
     * @brief 初始化Vulkan上下文（完整配置版本）
     * @param instanceConfig Instance配置
     * @param deviceConfig Device配置
     * @param windowHandle 窗口句柄（用于创建Surface）
     */
    void initialize(const InstanceConfig &instanceConfig, const DeviceConfig &deviceConfig,
                    void *windowHandle = nullptr);

    /**
     * @brief 创建Swapchain（使用配置）
     * @param config Swapchain配置
     */
    void createSwapchain(const SwapchainConfig &config);

    /**
     * @brief 重建Swapchain（用于窗口大小改变）
     * @param width 新的窗口宽度
     * @param height 新的窗口高度
     */
    void recreateSwapchain(uint32_t width, uint32_t height);

    /**
     * @brief 清理所有Vulkan资源
     */
    void cleanup();

    // ==================== Getter方法 ====================

    vk::Instance getVkInstance() const
    {
        return m_instance;
    }
    vk::PhysicalDevice getPhysicalDevice() const
    {
        return m_physicalDevice;
    }
    vk::Device getDevice() const
    {
        return m_device;
    }
    vk::SurfaceKHR getSurface() const
    {
        return m_surface;
    }
    vk::SwapchainKHR getSwapchain() const
    {
        return m_swapchain;
    }

    vk::Queue getGraphicsQueue() const
    {
        return m_graphicsQueue;
    }
    vk::Queue getPresentQueue() const
    {
        return m_presentQueue;
    }
    vk::Queue getComputeQueue() const
    {
        return m_computeQueue;
    }
    vk::Queue getTransferQueue() const
    {
        return m_transferQueue;
    }

    const QueueFamilyIndices &getQueueFamilyIndices() const
    {
        return m_queueFamilyIndices;
    }

    vk::Format getSwapchainImageFormat() const
    {
        return m_swapchainImageFormat;
    }
    vk::Extent2D getSwapchainExtent() const
    {
        return m_swapchainExtent;
    }
    const std::vector<vk::Image> &getSwapchainImages() const
    {
        return m_swapchainImages;
    }
    const std::vector<vk::ImageView> &getSwapchainImageViews() const
    {
        return m_swapchainImageViews;
    }

    uint32_t getSwapchainImageCount() const
    {
        return static_cast<uint32_t>(m_swapchainImages.size());
    }

    /**
     * @brief 获取物理设备属性
     */
    vk::PhysicalDeviceProperties getPhysicalDeviceProperties() const
    {
        return m_physicalDevice.getProperties();
    }

    /**
     * @brief 获取物理设备特性
     */
    vk::PhysicalDeviceFeatures getPhysicalDeviceFeatures() const
    {
        return m_physicalDevice.getFeatures();
    }

    /**
     * @brief 获取物理设备内存属性
     */
    vk::PhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties() const
    {
        return m_physicalDevice.getMemoryProperties();
    }

    /**
     * @brief 查找合适的内存类型
     * @param typeFilter 类型过滤器
     * @param properties 所需属性
     * @return uint32_t 内存类型索引
     */
    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;

    /**
     * @brief 等待设备空闲
     */
    void waitIdle() const
    {
        m_device.waitIdle();
    }

  private:
    VkContext() = default;
    ~VkContext();

    // ==================== 初始化方法 ====================

    /**
     * @brief 创建Vulkan实例
     */
    void createInstance(const std::string &appName, uint32_t appVersion, bool enableValidation);

    /**
     * @brief 创建Vulkan实例（使用配置）
     */
    void createInstance(const InstanceConfig &config);

    /**
     * @brief 设置调试信使
     */
    void setupDebugMessenger();

    /**
     * @brief 创建Surface
     */
    void createSurface(void *windowHandle);

    /**
     * @brief 选择物理设备
     */
    void pickPhysicalDevice();

    /**
     * @brief 创建逻辑设备（使用配置）
     */
    void createLogicalDevice(const DeviceConfig &config);

    /**
     * @brief 创建Swapchain（内部实现）
     */
    void createSwapchainInternal(uint32_t width, uint32_t height, bool vsync);

    /**
     * @brief 创建Swapchain Image Views
     */
    void createImageViews();

    /**
     * @brief 清理Swapchain Image Views
     */
    void cleanupImageViews();

    /**
     * @brief 清理Swapchain相关资源
     */
    void cleanupSwapchain();

    // ==================== 辅助方法 ====================

    /**
     * @brief 检查验证层是否可用
     */
    bool checkValidationLayerSupport() const;

    /**
     * @brief 获取所需的扩展
     */
    std::vector<const char *> getRequiredExtensions(bool enableValidation) const;

    /**
     * @brief 查找队列族
     */
    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device) const;

    /**
     * @brief 检查设备扩展支持
     */
    bool checkDeviceExtensionSupport(vk::PhysicalDevice device) const;

    /**
     * @brief 查询Swapchain支持详情
     */
    SwapchainSupportDetails querySwapchainSupport(vk::PhysicalDevice device) const;

    /**
     * @brief 评估物理设备是否合适
     */
    bool isDeviceSuitable(vk::PhysicalDevice device) const;

    /**
     * @brief 为物理设备评分（用于选择最佳设备）
     */
    int rateDeviceSuitability(vk::PhysicalDevice device) const;

    /**
     * @brief 选择最佳的Surface格式
     */
    vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats) const;

    /**
     * @brief 选择最佳的Present模式
     */
    vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes,
                                             bool vsync) const;

    /**
     * @brief 选择Swapchain的Extent
     */
    vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities, uint32_t width,
                                  uint32_t height) const;

    // ==================== 调试回调 ====================
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                        VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                        void *pUserData);

    // ==================== 成员变量 ====================

    // 核心对象
    vk::Instance m_instance;
    vk::PhysicalDevice m_physicalDevice;
    vk::Device m_device;
    vk::SurfaceKHR m_surface;
    vk::SwapchainKHR m_swapchain;

    // 队列
    vk::Queue m_graphicsQueue;
    vk::Queue m_presentQueue;
    vk::Queue m_computeQueue;
    vk::Queue m_transferQueue;

    QueueFamilyIndices m_queueFamilyIndices;

    // Swapchain相关
    vk::Format m_swapchainImageFormat;
    vk::Extent2D m_swapchainExtent;
    std::vector<vk::Image> m_swapchainImages;
    std::vector<vk::ImageView> m_swapchainImageViews;

    // 调试
    vk::DebugUtilsMessengerEXT m_debugMessenger;
    PFN_vkCreateDebugUtilsMessengerEXT m_pfnCreateDebugUtilsMessenger = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT m_pfnDestroyDebugUtilsMessenger = nullptr;

  public:
    // Debug Utils扩展函数指针（公开以供VkUtils等使用）
    PFN_vkSetDebugUtilsObjectNameEXT m_pfnSetDebugUtilsObjectName = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT m_pfnCmdBeginDebugUtilsLabel = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT m_pfnCmdEndDebugUtilsLabel = nullptr;
    PFN_vkCmdInsertDebugUtilsLabelEXT m_pfnCmdInsertDebugUtilsLabel = nullptr;

  private:
    bool m_enableValidation = false;

    // 配置缓存
    InstanceConfig m_instanceConfig;
    DeviceConfig m_deviceConfig;

    // 验证层（默认）
    std::vector<const char *> m_validationLayers = {"VK_LAYER_KHRONOS_validation"};

    // 设备扩展（默认）
    std::vector<const char *> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    bool m_initialized = false;
};

} // namespace vkcore
