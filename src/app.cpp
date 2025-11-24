#include "Render/Renderer/public/EngineServices.hpp"
#include "Render/Renderer/public/Renderer.hpp"
#include "UI/MainWindow.hpp"
#include "UI/VkRenderWindow.hpp"
#include <QApplication>
#include <QObject>
#include <QTimer>
#include <algorithm>
#include <array>
#include <filesystem>
#include <glm/glm.hpp>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace
{
std::filesystem::path findProjectRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty())
    {
        if (std::filesystem::exists(current / "assets"))
        {
            return current;
        }
        auto parent = current.parent_path();
        if (parent == current)
        {
            break;
        }
        current = parent;
    }
    return std::filesystem::current_path();
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 创建主窗口（栈对象，自动管理生命周期）
    MainWindow mainWindow;
    mainWindow.resize(1280, 720);
    VkRenderWindow *vkRenderWindowHandle = mainWindow.getVkRenderWindowHandle();
    auto winId = vkRenderWindowHandle->winId();
    renderer::EngineServices &services = renderer::EngineServices::instance();

    const std::filesystem::path projectRoot = findProjectRoot();
    const std::filesystem::path assetsRoot = projectRoot / "assets";
    const std::filesystem::path carAssetRoot = assetsRoot / "car";
    const std::filesystem::path shaderRoot = assetsRoot / "shaders/spv";

    vkcore::InstanceConfig instanceConfig;
    vkcore::DeviceConfig deviceConfig;
    vkcore::SwapchainConfig swapchainConfig;
    swapchainConfig.width = 1280;
    swapchainConfig.height = 720;
    swapchainConfig.vsync = true;

    auto &context =
        services.initializeVkContext(instanceConfig, deviceConfig, reinterpret_cast<void *>(winId), swapchainConfig);
    services.initializeResourceAllocator();
    services.initializeTransferManager();
    auto &allocator = services.getService<vkcore::VkResourceAllocator>();
    auto &transferManager = services.getService<vkcore::TransferManager>();
    auto &resourceManager = services.initializeResourceManager();
    auto &materialManager = services.initializeMaterialManager();
    auto &scene = services.initializeScene();

    renderer::RendererConfig rendererConfig{};
    rendererConfig.globalResources.meshFiles.push_back((carAssetRoot / "car.obj").string());
    rendererConfig.globalResources.textureFiles.push_back((carAssetRoot / "texture_pbr_20250901.png").string());
    rendererConfig.globalResources.textureFiles.push_back(
        (carAssetRoot / "texture_pbr_20250901_metallic.png").string());
    rendererConfig.globalResources.textureFiles.push_back(
        (carAssetRoot / "texture_pbr_20250901_roughness.png").string());
    rendererConfig.globalResources.textureFiles.push_back((carAssetRoot / "texture_pbr_20250901_normal.png").string());
    rendererConfig.globalResources.shaders.push_back({shaderRoot.string(), "car", false});
    rendererConfig.frameDefinition.shaderPrefix = "car";
    rendererConfig.swapchainAttachmentName = "Swapchain";

    renderer::RenderPassDefinition mainPass{};
    mainPass.name = "MainPass";
    mainPass.shaderPrefix = "car";

    renderer::RenderAttachment colorAttachment{};
    colorAttachment.type = renderer::AttachmentType::Color;
    colorAttachment.resourceName = rendererConfig.swapchainAttachmentName;
    colorAttachment.format = context.getSwapchainImageFormat();
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.clearValue = vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.02f, 0.02f, 0.02f, 1.0f}}};
    mainPass.resources.colorOutputs.push_back(colorAttachment);
    rendererConfig.renderPasses.addPass(mainPass);

    renderer::Renderer renderer(services, rendererConfig);

    // 创建相机、光源和渲染对象
    asset::Camera camera{};
    camera.position = {0.0f, 1.0f, 5.0f};
    camera.target = {0.0f, 0.5f, 0.0f};
    camera.aspect = static_cast<float>(swapchainConfig.width) / static_cast<float>(swapchainConfig.height);
    auto &cameraNode = scene.createCameraNode(camera);
    scene.setActiveCamera(cameraNode.id);

    asset::Light light{};
    light.color = {1.0f, 1.0f, 1.0f};
    light.intensity = 2.0f;
    light.direction = glm::normalize(glm::vec3{-1.0f, -1.0f, -1.0f});
    scene.createLightNode(light);

    auto meshId = resourceManager.loadMesh((carAssetRoot / "car.obj").string());
    auto materialId = materialManager.loadMaterialFromJson((carAssetRoot / "car.json").string());
    asset::RenderableComponent carRenderable{meshId, materialId, true};
    scene.createRenderableNode(carRenderable);

    struct GPUMesh
    {
        vkcore::ManagedBuffer vertexBuffer;
        vkcore::ManagedBuffer indexBuffer;
        uint32_t indexCount{0};
    };

    struct GPUTexture
    {
        vkcore::ManagedImage image;
        vkcore::ManagedSampler sampler;
    };

    auto uploadMesh = [&allocator, &transferManager, &resourceManager](const std::string &id) {
        GPUMesh gpuMesh{};
        auto meshDataList = resourceManager.getMesh(id);
        if (!meshDataList || meshDataList->empty())
        {
            throw std::runtime_error("Mesh data not found: " + id);
        }

        const auto &mesh = meshDataList->front();
        gpuMesh.indexCount = static_cast<uint32_t>(mesh.indices.size());

        vkcore::BufferDesc vtxDesc{};
        vtxDesc.size = mesh.getVertexDataSize();
        vtxDesc.usage = vkcore::BufferUsageFlags::Vertex | vkcore::BufferUsageFlags::TransferDst;
        vtxDesc.memory = vkcore::MemoryUsage::GpuOnly;
        vtxDesc.debugName = "CarVertexBuffer";
        gpuMesh.vertexBuffer = allocator.createBuffer(vtxDesc);
        auto vtxToken = transferManager.uploadToBuffer(gpuMesh.vertexBuffer, mesh.vertices.data(), 0);

        vkcore::BufferDesc idxDesc{};
        idxDesc.size = mesh.getIndexDataSize();
        idxDesc.usage = vkcore::BufferUsageFlags::Index | vkcore::BufferUsageFlags::TransferDst;
        idxDesc.memory = vkcore::MemoryUsage::GpuOnly;
        idxDesc.debugName = "CarIndexBuffer";
        gpuMesh.indexBuffer = allocator.createBuffer(idxDesc);
        auto idxToken = transferManager.uploadToBuffer(gpuMesh.indexBuffer, mesh.indices.data(), 0);

        vtxToken.wait();
        idxToken.wait();
        return gpuMesh;
    };

    auto uploadTexture = [&allocator, &transferManager, &resourceManager](const std::string &id) {
        GPUTexture texture{};
        auto textureData = resourceManager.getTexture(id);
        if (!textureData || !textureData->isValid())
        {
            throw std::runtime_error("Texture not found: " + id);
        }

        std::vector<unsigned char> pixelData;
        pixelData.assign(textureData->pixels, textureData->pixels + textureData->dataSize);
        // 确保是4通道数据
        if (textureData->channels == 3)
        {
            pixelData.clear();
            pixelData.reserve(textureData->width * textureData->height * 4);
            for (int i = 0; i < textureData->width * textureData->height; ++i)
            {
                pixelData.push_back(textureData->pixels[i * 3 + 0]);
                pixelData.push_back(textureData->pixels[i * 3 + 1]);
                pixelData.push_back(textureData->pixels[i * 3 + 2]);
                pixelData.push_back(255);
            }
        }

        vkcore::ImageDesc imageDesc{};
        imageDesc.width = static_cast<uint32_t>(textureData->width);
        imageDesc.height = static_cast<uint32_t>(textureData->height);
        imageDesc.format = vk::Format::eR8G8B8A8Unorm;
        imageDesc.usage = vkcore::ImageUsageFlags::Sampled | vkcore::ImageUsageFlags::TransferDst;
        texture.image = allocator.createImage(imageDesc, vk::ImageAspectFlagBits::eColor);

        auto token = transferManager.uploadToImage(texture.image, pixelData.data(), pixelData.size(), imageDesc.width,
                                                   imageDesc.height, 1);
        token.wait();

        texture.sampler = allocator.createSampler();
        return texture;
    };

    GPUMesh carMesh = uploadMesh(meshId);
    auto carMaterial = materialManager.getMaterial(materialId);
    GPUTexture baseColor = uploadTexture(carMaterial && !carMaterial->textures.baseColor.empty()
                                             ? carMaterial->textures.baseColor
                                             : (carAssetRoot / "texture_pbr_20250901.png").string());

    // 渲染循环相关资源
    vk::Device device = context.getDevice();
    vk::Queue graphicsQueue = context.getGraphicsQueue();
    vk::Queue presentQueue = context.getPresentQueue();
    auto graphicsFamily = context.getQueueFamilyIndices().graphicsFamily;
    if (!graphicsFamily)
    {
        throw std::runtime_error("Graphics queue family is not available");
    }
    uint32_t graphicsQueueFamily = *graphicsFamily;

    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;
    vk::CommandPool commandPool = device.createCommandPool(poolInfo);

    std::vector<vk::CommandBuffer> commandBuffers;
    std::vector<vk::ImageLayout> swapchainImageLayouts;

    auto allocateCommandBuffers = [&]() {
        if (!commandBuffers.empty())
        {
            device.freeCommandBuffers(commandPool, commandBuffers);
        }

        const auto swapchainImages = context.getSwapchainImages();
        commandBuffers.resize(swapchainImages.size());
        swapchainImageLayouts.assign(swapchainImages.size(), vk::ImageLayout::eUndefined);

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = commandPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
        if (allocInfo.commandBufferCount > 0)
        {
            commandBuffers = device.allocateCommandBuffers(allocInfo);
        }
    };

    allocateCommandBuffers();

    const size_t maxFramesInFlight = std::min<size_t>(rendererConfig.frameDefinition.framesInFlight, commandBuffers.size());
    std::vector<vk::Semaphore> imageAvailableSemaphores(maxFramesInFlight);
    std::vector<vk::Semaphore> renderFinishedSemaphores(maxFramesInFlight);
    std::vector<vk::Fence> inFlightFences(maxFramesInFlight);

    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
    for (size_t i = 0; i < maxFramesInFlight; ++i)
    {
        imageAvailableSemaphores[i] = device.createSemaphore(semaphoreInfo);
        renderFinishedSemaphores[i] = device.createSemaphore(semaphoreInfo);
        inFlightFences[i] = device.createFence(fenceInfo);
    }

    auto recreateSwapchainResources = [&]() {
        device.waitIdle();

        auto windowSize = vkRenderWindowHandle->size();
        if (windowSize.width() == 0 || windowSize.height() == 0)
        {
            return false;
        }

        renderer.onResize(
            {static_cast<uint32_t>(windowSize.width()), static_cast<uint32_t>(windowSize.height())});
        allocateCommandBuffers();
        return true;
    };

    size_t currentFrame = 0;
    auto drawFrame = [&]() {
        device.waitForFences(inFlightFences[currentFrame], VK_TRUE, std::numeric_limits<uint64_t>::max());

        auto acquired = device.acquireNextImageKHR(context.getSwapchain(), std::numeric_limits<uint64_t>::max(),
                                                   imageAvailableSemaphores[currentFrame], {});
        if (acquired.result == vk::Result::eErrorOutOfDateKHR)
        {
            if (!recreateSwapchainResources())
            {
                return;
            }
            return;
        }
        if (acquired.result != vk::Result::eSuccess && acquired.result != vk::Result::eSuboptimalKHR)
        {
            return;
        }

        uint32_t imageIndex = acquired.value;
        device.resetFences(inFlightFences[currentFrame]);

        vk::CommandBuffer cmd = commandBuffers[imageIndex];
        cmd.reset();
        vk::CommandBufferBeginInfo beginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        cmd.begin(beginInfo);

        const auto &swapchainImages = context.getSwapchainImages();
        if (imageIndex < swapchainImages.size())
        {
            vk::ImageMemoryBarrier toColor{};
            toColor.oldLayout = swapchainImageLayouts[imageIndex];
            toColor.newLayout = vk::ImageLayout::eAttachmentOptimal;
            toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toColor.image = swapchainImages[imageIndex];
            toColor.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            toColor.srcAccessMask = {};
            toColor.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

            vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
            if (swapchainImageLayouts[imageIndex] == vk::ImageLayout::ePresentSrcKHR)
            {
                srcStage = vk::PipelineStageFlagBits::eBottomOfPipe;
            }

            cmd.pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, nullptr, nullptr,
                                toColor);
        }
        renderer.recordFrame(cmd, imageIndex);

        if (imageIndex < swapchainImages.size())
        {
            vk::ImageMemoryBarrier toPresent{};
            toPresent.oldLayout = vk::ImageLayout::eAttachmentOptimal;
            toPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
            toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toPresent.image = swapchainImages[imageIndex];
            toPresent.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            toPresent.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            toPresent.dstAccessMask = {};

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                vk::PipelineStageFlagBits::eBottomOfPipe, {}, nullptr, nullptr, toPresent);
            swapchainImageLayouts[imageIndex] = vk::ImageLayout::ePresentSrcKHR;
        }
        cmd.end();

        vk::Semaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
        vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
        vk::Semaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};

        vk::SubmitInfo submitInfo{};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        graphicsQueue.submit(submitInfo, inFlightFences[currentFrame]);

        vk::SwapchainKHR swapchains[] = {context.getSwapchain()};
        vk::PresentInfoKHR presentInfo{};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;
        auto presentResult = presentQueue.presentKHR(presentInfo);
        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR ||
            presentResult == vk::Result::eErrorIncompatibleDisplayKHR)
        {
            recreateSwapchainResources();
        }

        currentFrame = (currentFrame + 1) % maxFramesInFlight;
    };

    QTimer *renderTimer = new QTimer(&mainWindow);
    QObject::connect(renderTimer, &QTimer::timeout, drawFrame);
    renderTimer->start(16); // ~60 FPS

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        device.waitIdle();
        for (size_t i = 0; i < maxFramesInFlight; ++i)
        {
            device.destroyFence(inFlightFences[i]);
            device.destroySemaphore(renderFinishedSemaphores[i]);
            device.destroySemaphore(imageAvailableSemaphores[i]);
        }
        device.destroyCommandPool(commandPool);
    });

    renderer.registerPassCallback("MainPass", [&](const renderer::RenderPassDefinition &,
                                                  const renderer::PassDrawContext &ctx) {
        vk::DescriptorSet cameraSet =
            ctx.frameResources.descriptorSets.size() > 0 ? ctx.frameResources.descriptorSets[0] : vk::DescriptorSet{};
        vk::DescriptorSet materialSet =
            ctx.frameResources.descriptorSets.size() > 1 ? ctx.frameResources.descriptorSets[1] : vk::DescriptorSet{};

        auto device = context.getDevice();
        if (ctx.frameResources.descriptorSchemas.size() > 0 && cameraSet)
        {
            auto writer =
                vkcore::DescriptorSetWriter::begin(device, ctx.frameResources.descriptorSchemas[0], cameraSet);
            writer.writeBuffer("uCamera", ctx.frameResources.cameraBuffer)
                .writeBuffer("uLight", ctx.frameResources.lightBuffer)
                .update();
        }

        if (ctx.frameResources.descriptorSchemas.size() > 1 && materialSet)
        {
            auto writer =
                vkcore::DescriptorSetWriter::begin(device, ctx.frameResources.descriptorSchemas[1], materialSet);
            writer
                .writeSampledImage("baseColorTex", baseColor.image, baseColor.sampler,
                                   vk::ImageLayout::eShaderReadOnlyOptimal)
                .update();
        }

        std::vector<vk::DescriptorSet> sets;
        if (cameraSet)
        {
            sets.push_back(cameraSet);
        }
        if (materialSet)
        {
            sets.push_back(materialSet);
        }

        if (ctx.pipelineLayout && !sets.empty())
        {
            ctx.cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ctx.pipelineLayout, 0, sets, {});
        }

        vk::Buffer vertexBuffers[] = {carMesh.vertexBuffer.getBuffer()};
        vk::DeviceSize offsets[] = {0};
        ctx.cmd.bindVertexBuffers(0, 1, vertexBuffers, offsets);
        ctx.cmd.bindIndexBuffer(carMesh.indexBuffer.getBuffer(), 0, vk::IndexType::eUint32);
        ctx.cmd.drawIndexed(carMesh.indexCount, 1, 0, 0, 0);
    });

    mainWindow.show();

    return app.exec();
}