#include "Render/Asset/ResourceManager/public/ResourceManager.hpp"
#include "Render/VkCore/public/TransferManager.hpp"
#include "Render/VkCore/public/VkContext.hpp"
#include "UI/MainWindow.hpp"
#include "UI/VkRenderWindow.hpp"
#include <QApplication>
#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 创建主窗口（栈对象，自动管理生命周期）
    MainWindow mainWindow;
    mainWindow.resize(1280, 720);
    VkRenderWindow *vkRenderWindowHandle = mainWindow.getVkRenderWindowHandle();
    auto winId = vkRenderWindowHandle->winId();
    vkcore::InstanceConfig instanceConfig;
    vkcore::DeviceConfig deviceConfig;

    // 初始化Vulkan上下文
    vkcore::VkContext::getInstance().initialize(instanceConfig, deviceConfig, reinterpret_cast<void *>(winId));

    vkcore::SwapchainConfig swapchainConfig;
    swapchainConfig.width = 1280;
    swapchainConfig.height = 720;
    swapchainConfig.vsync = true;

    vkcore::VkContext::getInstance().createSwapchain(swapchainConfig);
    vkcore::VkResourceAllocator resourceAllocator;
    resourceAllocator.initialize(vkcore::VkContext::getInstance());

    vkcore::TransferManager transferManager;
    transferManager.initialize(vkcore::VkContext::getInstance(), resourceAllocator);
    asset::ResourceManager resourceManager(vkcore::VkContext::getInstance());
    std::string shaderId =
        resourceManager.loadShader("E:\\Github_repo\\QTRender_v2\\assets\\shaders\\spv", "test", true);
    std::cout << "Loaded shader ID: " << shaderId << std::endl;
    std::string meshId = resourceManager.loadMesh("E:\\Github_repo\\QTRender_v2\\assets\\car\\car.obj");
    std::cout << "Loaded mesh ID: " << meshId << std::endl;
    std::string textureId =
        resourceManager.loadTexture("E:\\Github_repo\\QTRender_v2\\assets\\car\\texture_pbr_20250901_metallic.png");
    std::cout << "Loaded texture ID: " << textureId << std::endl;
    auto schmas = resourceManager.getShaderDescriptorSchemas(shaderId);
    auto descriptorSets = resourceManager.getOrAllocateDescriptorSet(schmas, "test");
    auto defaultCubeMeshes = resourceManager.getMesh("default_cube");
    auto defaultWhiteTexture = resourceManager.getTexture("default_white");
    std::cout << "Default cube mesh vertices count: " << defaultCubeMeshes->at(0).vertices.size() << std::endl;
    std::cout << "Default white texture size: " << defaultWhiteTexture->width << "x" << defaultWhiteTexture->height
              << std::endl;
    mainWindow.show();

    return app.exec();
}