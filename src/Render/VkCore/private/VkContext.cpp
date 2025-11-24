#include "../public/VkContext.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#undef max
#undef min

using namespace vkcore;

VkContext &VkContext::getInstance()
{
    static VkContext s;
    return s;
}

VkContext::~VkContext()
{
    if (m_initialized)
        cleanup();
}

void VkContext::initialize(const InstanceConfig &instanceConfig, const DeviceConfig &deviceConfig, void *windowHandle)
{
    if (m_initialized)
        return;

    m_instanceConfig = instanceConfig;
    m_deviceConfig = deviceConfig;
    m_enableValidation = instanceConfig.enableValidation;

    createInstance(instanceConfig);

    // 加载扩展函数指针
    if (m_enableValidation)
    {
        m_pfnCreateDebugUtilsMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            m_instance.getProcAddr("vkCreateDebugUtilsMessengerEXT"));
        m_pfnDestroyDebugUtilsMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            m_instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
        setupDebugMessenger();
    }

    createSurface(windowHandle);
    pickPhysicalDevice();
    createLogicalDevice(deviceConfig);

    m_initialized = true;
}

void VkContext::createInstance(const std::string &appName, uint32_t appVersion, bool enableValidation)
{
    // 初始化默认分发加载器以加载基础Vulkan函数
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = appName.c_str();
    appInfo.applicationVersion = appVersion;
    appInfo.pEngineName = "QTRender_v2";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    auto extensions = getRequiredExtensions(enableValidation);

    vk::InstanceCreateInfo createInfo;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (enableValidation && checkValidationLayerSupport())
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    m_instance = vk::createInstance(createInfo);
    if (!m_instance)
        throw std::runtime_error("failed to create Vulkan instance");

    // 初始化实例级函数
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);

    // 加载Debug Utils扩展函数
    if (enableValidation)
    {
        m_pfnSetDebugUtilsObjectName =
            reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(m_instance.getProcAddr("vkSetDebugUtilsObjectNameEXT"));
        m_pfnCmdBeginDebugUtilsLabel =
            reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(m_instance.getProcAddr("vkCmdBeginDebugUtilsLabelEXT"));
        m_pfnCmdEndDebugUtilsLabel =
            reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(m_instance.getProcAddr("vkCmdEndDebugUtilsLabelEXT"));
        m_pfnCmdInsertDebugUtilsLabel = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
            m_instance.getProcAddr("vkCmdInsertDebugUtilsLabelEXT"));
    }
}

void VkContext::createInstance(const InstanceConfig &config)
{
    // 初始化默认分发加载器以加载基础Vulkan函数
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = config.appName.c_str();
    appInfo.applicationVersion = config.appVersion;
    appInfo.pEngineName = config.engineName.c_str();
    appInfo.engineVersion = config.engineVersion;
    appInfo.apiVersion = config.apiVersion;

    std::vector<const char *> extensions = config.instanceExtensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

    // 平台相关的Surface扩展
#if defined(_WIN32)
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
    extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(__APPLE__)
    extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif

    if (config.enableValidation)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // 创建Debug Messenger CreateInfo（用于pNext链，捕获早期验证错误）
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (config.enableValidation && checkValidationLayerSupport())
    {
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        debugCreateInfo.pUserData = nullptr;
    }

    vk::InstanceCreateInfo createInfo;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    // 将Debug Messenger加入pNext链
    if (config.enableValidation && checkValidationLayerSupport())
    {
        createInfo.pNext = &debugCreateInfo;
        createInfo.enabledLayerCount = static_cast<uint32_t>(config.validationLayers.size());
        createInfo.ppEnabledLayerNames = config.validationLayers.data();
    }

    m_instance = vk::createInstance(createInfo);
    if (!m_instance)
        throw std::runtime_error("failed to create Vulkan instance");

    // 初始化实例级函数
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);

    // 加载Debug Utils扩展函数
    if (config.enableValidation)
    {
        m_pfnSetDebugUtilsObjectName =
            reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(m_instance.getProcAddr("vkSetDebugUtilsObjectNameEXT"));
        m_pfnCmdBeginDebugUtilsLabel =
            reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(m_instance.getProcAddr("vkCmdBeginDebugUtilsLabelEXT"));
        m_pfnCmdEndDebugUtilsLabel =
            reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(m_instance.getProcAddr("vkCmdEndDebugUtilsLabelEXT"));
        m_pfnCmdInsertDebugUtilsLabel = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
            m_instance.getProcAddr("vkCmdInsertDebugUtilsLabelEXT"));
    }
}

void VkContext::setupDebugMessenger()
{
    if (!m_enableValidation || !m_pfnCreateDebugUtilsMessenger)
        return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = nullptr;

    VkDebugUtilsMessengerEXT rawMessenger;
    if (m_pfnCreateDebugUtilsMessenger(static_cast<VkInstance>(m_instance), &createInfo, nullptr, &rawMessenger) !=
        VK_SUCCESS)
    {
        throw std::runtime_error("failed to create debug messenger!");
    }
    m_debugMessenger = rawMessenger;
}

void VkContext::createSurface(void *windowHandle)
{
    if (!windowHandle)
    {
        // 不创建 surface，用户可以后续提供
        m_surface = nullptr;
        return;
    }

#ifdef VK_USE_PLATFORM_WIN32_KHR
    HWND hwnd = static_cast<HWND>(windowHandle);
    HINSTANCE hinstance = GetModuleHandle(nullptr);

    vk::Win32SurfaceCreateInfoKHR createInfo;
    createInfo.hinstance = hinstance;
    createInfo.hwnd = hwnd;

    m_surface = m_instance.createWin32SurfaceKHR(createInfo);
    if (!m_surface)
        throw std::runtime_error("failed to create Win32 surface");
#else
    throw std::runtime_error("Win32 surface creation not supported on this platform");
#endif
}

void VkContext::pickPhysicalDevice()
{
    auto devices = m_instance.enumeratePhysicalDevices();
    if (devices.empty())
        throw std::runtime_error("failed to find GPUs with Vulkan support");

    int bestScore = -1;
    vk::PhysicalDevice bestDevice;

    for (auto &device : devices)
    {
        if (!isDeviceSuitable(device))
            continue;

        int score = rateDeviceSuitability(device);
        if (score > bestScore)
        {
            bestScore = score;
            bestDevice = device;
        }
    }

    if (!bestDevice)
        throw std::runtime_error("failed to find a suitable GPU");

    m_physicalDevice = bestDevice;
}

void VkContext::createLogicalDevice(const DeviceConfig &config)
{
    m_queueFamilyIndices = findQueueFamilies(m_physicalDevice);

    // 收集所有需要的队列族（去重）
    std::set<uint32_t> uniqueQueueFamilies = {m_queueFamilyIndices.graphicsFamily.value(),
                                              m_queueFamilyIndices.presentFamily.value()};

    // 添加可选的队列族
    if (m_queueFamilyIndices.computeFamily.has_value())
        uniqueQueueFamilies.insert(m_queueFamilyIndices.computeFamily.value());
    if (m_queueFamilyIndices.transferFamily.has_value())
        uniqueQueueFamilies.insert(m_queueFamilyIndices.transferFamily.value());

    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    for (uint32_t qf : uniqueQueueFamilies)
    {
        vk::DeviceQueueCreateInfo qi;
        qi.queueFamilyIndex = qf;
        qi.queueCount = 1;
        qi.pQueuePriorities = &config.queuePriority;
        queueCreateInfos.push_back(qi);
    }

    // Vulkan 1.3 特性
    vk::PhysicalDeviceVulkan13Features features13{};
    features13.dynamicRendering = config.enableDynamicRendering;
    features13.synchronization2 = config.enableSynchronization2;
    features13.maintenance4 = config.enableMaintenance4;

    // Vulkan 1.2 特性
    vk::PhysicalDeviceVulkan12Features features12{};
    features12.pNext = &features13;
    features12.descriptorIndexing = config.enableDescriptorIndexing;
    features12.bufferDeviceAddress = config.enableBufferDeviceAddress;
    features12.timelineSemaphore = config.enableTimelineSemaphore;
    features12.scalarBlockLayout = config.enableScalarBlockLayout;
    features12.uniformAndStorageBuffer8BitAccess = config.enableUniformAndStorageBuffer8BitAccess;
    features12.shaderFloat16 = config.enableShaderFloat16;
    features12.shaderInt8 = config.enableShaderInt8;

    // Vulkan 1.1 特性
    vk::PhysicalDeviceVulkan11Features features11{};
    features11.pNext = &features12;
    features11.shaderDrawParameters = config.enableShaderDrawParameters;

    // 扩展列表
    std::vector<const char *> extensions = config.deviceExtensions;
    m_deviceExtensions = extensions; // 同步到成员变量，供检查使用

    vk::DeviceCreateInfo createInfo;
    createInfo.pNext = &features11;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &config.features10;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (m_enableValidation && checkValidationLayerSupport())
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        createInfo.ppEnabledLayerNames = m_validationLayers.data();
    }

    m_device = m_physicalDevice.createDevice(createInfo);

    // 初始化设备级函数
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);

    m_graphicsQueue = m_device.getQueue(m_queueFamilyIndices.graphicsFamily.value(), 0);
    m_presentQueue = m_device.getQueue(m_queueFamilyIndices.presentFamily.value(), 0);
    if (m_queueFamilyIndices.computeFamily.has_value())
        m_computeQueue = m_device.getQueue(m_queueFamilyIndices.computeFamily.value(), 0);
    if (m_queueFamilyIndices.transferFamily.has_value())
        m_transferQueue = m_device.getQueue(m_queueFamilyIndices.transferFamily.value(), 0);
}

void VkContext::createSwapchain(const SwapchainConfig &config)
{
    if (!m_surface)
        return;

    // 检查窗口大小，避免最小化时创建Swapchain
    if (config.width == 0 || config.height == 0)
    {
        std::cerr << "Warning: Window size is zero, skipping swapchain creation" << std::endl;
        return;
    }

    auto swapDetails = querySwapchainSupport(m_physicalDevice);

    // 选择格式
    vk::SurfaceFormatKHR surfaceFormat = swapDetails.formats[0];
    for (const auto &available : swapDetails.formats)
    {
        if (available.format == config.preferredFormat && available.colorSpace == config.preferredColorSpace)
        {
            surfaceFormat = available;
            break;
        }
    }

    // 选择呈现模式
    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
    if (!config.vsync)
    {
        for (const auto &mode : swapDetails.presentModes)
        {
            if (mode == config.preferredPresentMode)
            {
                presentMode = mode;
                break;
            }
        }
    }

    auto extent = chooseSwapExtent(swapDetails.capabilities, config.width, config.height);

    uint32_t imageCount = config.imageCount;
    if (imageCount == 0)
        imageCount = swapDetails.capabilities.minImageCount + 1;
    if (swapDetails.capabilities.maxImageCount > 0 && imageCount > swapDetails.capabilities.maxImageCount)
        imageCount = swapDetails.capabilities.maxImageCount;

    vk::SwapchainCreateInfoKHR createInfo;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    uint32_t queueFamilyIndices[] = {m_queueFamilyIndices.graphicsFamily.value(),
                                     m_queueFamilyIndices.presentFamily.value()};
    if (m_queueFamilyIndices.graphicsFamily != m_queueFamilyIndices.presentFamily)
    {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    createInfo.preTransform = swapDetails.capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = m_swapchain;

    if (m_swapchain)
        m_device.destroySwapchainKHR(m_swapchain);

    m_swapchain = m_device.createSwapchainKHR(createInfo);
    m_swapchainImages = m_device.getSwapchainImagesKHR(m_swapchain);
    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;

    createImageViews();
}

void VkContext::createSwapchainInternal(uint32_t width, uint32_t height, bool vsync)
{
    if (!m_surface)
        return; // 无 surface，则不创建

    auto swapDetails = querySwapchainSupport(m_physicalDevice);
    auto surfaceFormat = chooseSwapSurfaceFormat(swapDetails.formats);
    auto presentMode = chooseSwapPresentMode(swapDetails.presentModes, vsync);
    auto extent = chooseSwapExtent(swapDetails.capabilities, width, height);

    uint32_t imageCount = swapDetails.capabilities.minImageCount + 1;
    if (swapDetails.capabilities.maxImageCount > 0 && imageCount > swapDetails.capabilities.maxImageCount)
        imageCount = swapDetails.capabilities.maxImageCount;

    vk::SwapchainCreateInfoKHR createInfo;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    uint32_t queueFamilyIndices[] = {m_queueFamilyIndices.graphicsFamily.value(),
                                     m_queueFamilyIndices.presentFamily.value()};
    if (m_queueFamilyIndices.graphicsFamily != m_queueFamilyIndices.presentFamily)
    {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    }

    createInfo.preTransform = swapDetails.capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = m_swapchain;

    if (m_swapchain)
        m_device.destroySwapchainKHR(m_swapchain);

    m_swapchain = m_device.createSwapchainKHR(createInfo);
    m_swapchainImages = m_device.getSwapchainImagesKHR(m_swapchain);
    m_swapchainImageFormat = surfaceFormat.format;
    m_swapchainExtent = extent;

    createImageViews();
}

void VkContext::recreateSwapchain(uint32_t width, uint32_t height)
{
    cleanupSwapchain();
    createSwapchainInternal(width, height, true);
}

void VkContext::createImageViews()
{
    cleanupImageViews();

    m_swapchainImageViews.reserve(m_swapchainImages.size());
    for (auto image : m_swapchainImages)
    {
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = m_swapchainImageFormat;
        viewInfo.components = vk::ComponentMapping();
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        auto view = m_device.createImageView(viewInfo);
        m_swapchainImageViews.push_back(view);
    }
}

void VkContext::cleanupImageViews()
{
    for (auto &view : m_swapchainImageViews)
    {
        if (view)
            m_device.destroyImageView(view);
    }
    m_swapchainImageViews.clear();
}

void VkContext::cleanupSwapchain()
{
    cleanupImageViews();
    m_swapchainImages.clear();

    if (m_swapchain)
    {
        m_device.destroySwapchainKHR(m_swapchain);
        m_swapchain = nullptr;
    }
}

void VkContext::cleanup()
{
    // 1. 等待设备空闲
    if (m_device)
        m_device.waitIdle();

    // 2. 销毁所有设备相关对象
    cleanupSwapchain();

    // 3. 销毁逻辑设备
    if (m_device)
        m_device.destroy();

    // 4. 销毁Surface
    if (m_surface)
        m_instance.destroySurfaceKHR(m_surface);

    // 5. 销毁Debug Messenger
    if (m_debugMessenger && m_enableValidation && m_pfnDestroyDebugUtilsMessenger)
    {
        m_pfnDestroyDebugUtilsMessenger(static_cast<VkInstance>(m_instance),
                                        static_cast<VkDebugUtilsMessengerEXT>(m_debugMessenger), nullptr);
    }

    // 6. 最后销毁实例
    if (m_instance)
        m_instance.destroy();

    m_initialized = false;
}

bool VkContext::checkValidationLayerSupport() const
{
    auto availableLayers = vk::enumerateInstanceLayerProperties();
    bool allFound = true;

    for (const char *layerName : m_validationLayers)
    {
        bool found = false;
        for (auto &layerProp : availableLayers)
        {
            if (strcmp(layerName, layerProp.layerName) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            std::cerr << "Validation layer not found: " << layerName << std::endl;
            allFound = false;
        }
    }
    return allFound;
}

std::vector<const char *> VkContext::getRequiredExtensions(bool enableValidation) const
{
    std::vector<const char *> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);

    // 平台相关的Surface扩展
#if defined(_WIN32)
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
    extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
    // 或者 VK_KHR_XLIB_SURFACE_EXTENSION_NAME
#elif defined(__APPLE__)
    extensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif

    if (enableValidation)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

QueueFamilyIndices VkContext::findQueueFamilies(vk::PhysicalDevice device) const
{
    QueueFamilyIndices indices;

    auto queueFamilies = device.getQueueFamilyProperties();

    uint32_t i = 0;
    for (const auto &queueFamily : queueFamilies)
    {
        // Graphics队列
        if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
            indices.graphicsFamily = i;

        // 优先选择专用Compute队列（不支持Graphics）
        if ((queueFamily.queueFlags & vk::QueueFlagBits::eCompute) &&
            !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
        {
            indices.computeFamily = i;
        }
        else if (!indices.computeFamily.has_value() && (queueFamily.queueFlags & vk::QueueFlagBits::eCompute))
        {
            // 如果没有专用队列，使用任何支持Compute的队列
            indices.computeFamily = i;
        }

        // 优先选择专用Transfer队列（不支持Graphics和Compute）
        if ((queueFamily.queueFlags & vk::QueueFlagBits::eTransfer) &&
            !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) &&
            !(queueFamily.queueFlags & vk::QueueFlagBits::eCompute))
        {
            indices.transferFamily = i;
        }
        else if (!indices.transferFamily.has_value() && (queueFamily.queueFlags & vk::QueueFlagBits::eTransfer))
        {
            // 如果没有专用队列，使用任何支持Transfer的队列
            indices.transferFamily = i;
        }

        // Present支持检查
        if (m_surface)
        {
            VkBool32 presentSupport = device.getSurfaceSupportKHR(i, m_surface);
            if (presentSupport)
                indices.presentFamily = i;
        }

        ++i;
    }

    return indices;
}

bool VkContext::checkDeviceExtensionSupport(vk::PhysicalDevice device) const
{
    auto availableExtensions = device.enumerateDeviceExtensionProperties();
    std::set<std::string> required(m_deviceExtensions.begin(), m_deviceExtensions.end());

    for (const auto &ext : availableExtensions)
    {
        required.erase(ext.extensionName);
    }

    return required.empty();
}

SwapchainSupportDetails VkContext::querySwapchainSupport(vk::PhysicalDevice device) const
{
    SwapchainSupportDetails details;
    details.capabilities = device.getSurfaceCapabilitiesKHR(m_surface);
    details.formats = device.getSurfaceFormatsKHR(m_surface);
    details.presentModes = device.getSurfacePresentModesKHR(m_surface);
    return details;
}

bool VkContext::isDeviceSuitable(vk::PhysicalDevice device) const
{
    auto indices = findQueueFamilies(device);
    if (!indices.isComplete())
        return false;

    if (!checkDeviceExtensionSupport(device))
        return false;

    auto swapDetails = querySwapchainSupport(device);
    bool swapAdequate = !swapDetails.formats.empty() && !swapDetails.presentModes.empty();

    vk::PhysicalDeviceFeatures supportedFeatures = device.getFeatures();

    return swapAdequate && supportedFeatures.samplerAnisotropy;
}

int VkContext::rateDeviceSuitability(vk::PhysicalDevice device) const
{
    vk::PhysicalDeviceProperties props = device.getProperties();
    vk::PhysicalDeviceFeatures feats = device.getFeatures();

    int score = 0;
    if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        score += 1000;

    score += props.limits.maxImageDimension2D;

    if (!feats.geometryShader)
        return 0;

    return score;
}

vk::SurfaceFormatKHR VkContext::chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &availableFormats) const
{
    if (availableFormats.size() == 1 && availableFormats[0].format == vk::Format::eUndefined)
    {
        return {vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear};
    }

    for (const auto &available : availableFormats)
    {
        if (available.format == vk::Format::eB8G8R8A8Unorm && available.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
            return available;
    }

    return availableFormats[0];
}

vk::PresentModeKHR VkContext::chooseSwapPresentMode(const std::vector<vk::PresentModeKHR> &availablePresentModes,
                                                    bool vsync) const
{
    if (!vsync)
    {
        for (const auto &mode : availablePresentModes)
        {
            if (mode == vk::PresentModeKHR::eMailbox)
                return mode;
            if (mode == vk::PresentModeKHR::eImmediate)
                return mode;
        }
    }

    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D VkContext::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities, uint32_t width,
                                         uint32_t height) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    else
    {
        vk::Extent2D actualExtent = {width, height};
        actualExtent.width =
            std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height =
            std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return actualExtent;
    }
}

uint32_t VkContext::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const
{
    auto memProperties = m_physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1u << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    throw std::runtime_error("failed to find suitable memory type");
}

VKAPI_ATTR VkBool32 VKAPI_CALL VkContext::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                        VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                                        void *pUserData)
{
    std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}
