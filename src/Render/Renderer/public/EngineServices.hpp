#pragma once

#include "MaterialManager.hpp"
#include "ResourceManager.hpp"
#include "Scene.hpp"
#include "TransferManager.hpp"
#include "VkContext.hpp"
#include "VkResource.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>

namespace renderer
{

/**
 * @class EngineServices
 * @brief 引擎服务定位与生命周期管理器。
 *
 * EngineServices 为核心子系统提供统一的注册、获取与初始化接口，覆盖 Vulkan 上下文、
 * 资源分配与上传、资源/材质管理以及场景管理等主要服务。典型用法：
 * - 通过 @ref initializeVkContext 与 @ref initializeResourceAllocator 按依赖顺序构建底层设施
 * - 使用 @ref initializeTransferManager、@ref initializeResourceManager、@ref initializeMaterialManager
 *   和 @ref initializeScene 创建高层服务
 * - 可通过 @ref registerExternalService 注册外部创建的服务实例
 */
class EngineServices
{
  public:
    /**
     * @brief 获取全局 EngineServices 实例。
     */
    static EngineServices &instance();

    EngineServices(const EngineServices &) = delete;
    EngineServices &operator=(const EngineServices &) = delete;

    /**
     * @brief 创建并注册一个由 EngineServices 管理生命周期的服务。
     * @tparam Service 服务类型。
     * @tparam Args 构造参数类型。
     * @param args 转发到服务构造函数的参数。
     * @return 注册后的服务引用。
     */
    template <typename Service, typename... Args> Service &emplaceService(Args &&...args)
    {
        auto holder = std::make_unique<OwningHolder<Service>>(std::make_unique<Service>(std::forward<Args>(args)...));
        Service *ptr = holder->get();
        m_services[std::type_index(typeid(Service))] = std::move(holder);
        return *ptr;
    }

    /**
     * @brief 注册一个由外部管理生命周期的服务。
     * @tparam Service 服务类型。
     * @param service 外部服务实例引用。
     */
    template <typename Service> void registerExternalService(Service &service)
    {
        m_services[std::type_index(typeid(Service))] = std::make_unique<NonOwningHolder<Service>>(&service);
    }

    /**
     * @brief 检查指定类型的服务是否已注册。
     */
    template <typename Service> [[nodiscard]] bool hasService() const
    {
        return m_services.find(std::type_index(typeid(Service))) != m_services.end();
    }

    /**
     * @brief 获取指定类型的服务指针，未注册时返回 nullptr。
     */
    template <typename Service> [[nodiscard]] Service *tryGetService() const
    {
        auto iter = m_services.find(std::type_index(typeid(Service)));
        if (iter == m_services.end())
        {
            return nullptr;
        }
        return static_cast<Service *>(iter->second->get());
    }

    /**
     * @brief 获取指定类型的服务引用，不存在时抛出异常。
     */
    template <typename Service> Service &getService() const
    {
        auto *ptr = tryGetService<Service>();
        if (!ptr)
        {
            throw std::runtime_error("Requested service is not registered");
        }
        return *ptr;
    }

    /**
     * @brief 清空所有已注册的服务。
     */
    void clear();

    // ==================== 初始化辅助 ====================

    /**
     * @brief 初始化 Vulkan 上下文，并可选创建交换链。
     * @param instanceConfig Instance 配置。
     * @param deviceConfig Device 配置。
     * @param windowHandle 平台窗口句柄（用于创建 Surface）。
     * @param swapchainConfig 若提供，则在初始化后立即创建交换链。
     * @return VkContext 引用。
     */
    vkcore::VkContext &initializeVkContext(
        const vkcore::InstanceConfig &instanceConfig, const vkcore::DeviceConfig &deviceConfig,
        void *windowHandle = nullptr, const std::optional<vkcore::SwapchainConfig> &swapchainConfig = std::nullopt);

    /**
     * @brief 初始化 Vulkan 资源分配器。
     * @return VkResourceAllocator 引用。
     */
    vkcore::VkResourceAllocator &initializeResourceAllocator();

    /**
     * @brief 初始化传输管理器。
     * @param config 传输管理器配置。
     * @return TransferManager 引用。
     */
    vkcore::TransferManager &initializeTransferManager(const vkcore::TransferManagerConfig &config = {});

    /**
     * @brief 初始化资源管理器。
     * @return ResourceManager 引用。
     */
    asset::ResourceManager &initializeResourceManager();

    /**
     * @brief 初始化材质管理器。
     * @return MaterialManager 引用。
     */
    asset::MaterialManager &initializeMaterialManager();

    /**
     * @brief 初始化场景容器。
     * @return Scene 引用。
     */
    asset::Scene &initializeScene();

  private:
    EngineServices() = default;
    ~EngineServices() = default;

    struct IServiceHolder
    {
        virtual ~IServiceHolder() = default;
        virtual void *get() const = 0;
    };

    template <typename Service> struct OwningHolder final : IServiceHolder
    {
        explicit OwningHolder(std::unique_ptr<Service> ptr) : m_ptr(std::move(ptr))
        {
        }
        void *get() const override
        {
            return m_ptr.get();
        }
        std::unique_ptr<Service> m_ptr;
    };

    template <typename Service> struct NonOwningHolder final : IServiceHolder
    {
        explicit NonOwningHolder(Service *ptr) : m_ptr(ptr)
        {
        }
        void *get() const override
        {
            return m_ptr;
        }
        Service *m_ptr;
    };

    std::unordered_map<std::type_index, std::unique_ptr<IServiceHolder>> m_services;
};

inline EngineServices &EngineServices::instance()
{
    static EngineServices instance;
    return instance;
}

inline void EngineServices::clear()
{
    m_services.clear();
}

inline vkcore::VkContext &EngineServices::initializeVkContext(
    const vkcore::InstanceConfig &instanceConfig, const vkcore::DeviceConfig &deviceConfig, void *windowHandle,
    const std::optional<vkcore::SwapchainConfig> &swapchainConfig)
{
    auto &ctx = vkcore::VkContext::getInstance();
    if (!hasService<vkcore::VkContext>())
    {
        ctx.initialize(instanceConfig, deviceConfig, windowHandle);
        if (swapchainConfig)
        {
            ctx.createSwapchain(*swapchainConfig);
        }
        registerExternalService(ctx);
    }
    return ctx;
}

inline vkcore::VkResourceAllocator &EngineServices::initializeResourceAllocator()
{
    auto *ctx = tryGetService<vkcore::VkContext>();
    if (!ctx)
    {
        throw std::runtime_error("VkContext must be initialized before VkResourceAllocator");
    }
    if (auto *existing = tryGetService<vkcore::VkResourceAllocator>())
    {
        return *existing;
    }
    auto &allocator = emplaceService<vkcore::VkResourceAllocator>();
    allocator.initialize(*ctx);
    return allocator;
}

inline vkcore::TransferManager &EngineServices::initializeTransferManager(const vkcore::TransferManagerConfig &config)
{
    auto *ctx = tryGetService<vkcore::VkContext>();
    auto *allocator = tryGetService<vkcore::VkResourceAllocator>();
    if (!ctx || !allocator)
    {
        throw std::runtime_error("VkContext and VkResourceAllocator must be initialized before TransferManager");
    }
    if (auto *existing = tryGetService<vkcore::TransferManager>())
    {
        return *existing;
    }
    auto &transfer = emplaceService<vkcore::TransferManager>();
    transfer.initialize(*ctx, *allocator, config);
    return transfer;
}

inline asset::ResourceManager &EngineServices::initializeResourceManager()
{
    auto *ctx = tryGetService<vkcore::VkContext>();
    if (!ctx)
    {
        throw std::runtime_error("VkContext must be initialized before ResourceManager");
    }
    if (auto *existing = tryGetService<asset::ResourceManager>())
    {
        return *existing;
    }
    return emplaceService<asset::ResourceManager>(*ctx);
}

inline asset::MaterialManager &EngineServices::initializeMaterialManager()
{
    auto *resourceManager = tryGetService<asset::ResourceManager>();
    if (!resourceManager)
    {
        throw std::runtime_error("ResourceManager must be initialized before MaterialManager");
    }
    if (auto *existing = tryGetService<asset::MaterialManager>())
    {
        return *existing;
    }
    return emplaceService<asset::MaterialManager>(*resourceManager);
}

inline asset::Scene &EngineServices::initializeScene()
{
    if (auto *existing = tryGetService<asset::Scene>())
    {
        return *existing;
    }
    return emplaceService<asset::Scene>();
}

} // namespace renderer
