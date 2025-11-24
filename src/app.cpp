#include "Render/Renderer/public/EngineServices.hpp"
#include "Render/Renderer/public/Renderer.hpp"
#include "UI/MainWindow.hpp"
#include "UI/VkRenderWindow.hpp"
#include <QApplication>
#include <array>
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 创建主窗口（栈对象，自动管理生命周期）
    MainWindow mainWindow;
    mainWindow.resize(1280, 720);
    VkRenderWindow *vkRenderWindowHandle = mainWindow.getVkRenderWindowHandle();
    auto winId = vkRenderWindowHandle->winId();
    renderer::EngineServices &services = renderer::EngineServices::instance();

    vkcore::InstanceConfig instanceConfig;
    vkcore::DeviceConfig deviceConfig;
    vkcore::SwapchainConfig swapchainConfig;
    swapchainConfig.width = 1280;
    swapchainConfig.height = 720;
    swapchainConfig.vsync = true;

    auto &context = services.initializeVkContext(instanceConfig, deviceConfig, reinterpret_cast<void *>(winId), swapchainConfig);
    services.initializeResourceAllocator();
    services.initializeTransferManager();
    auto &allocator = services.getService<vkcore::VkResourceAllocator>();
    auto &transferManager = services.getService<vkcore::TransferManager>();
    auto &resourceManager = services.initializeResourceManager();
    auto &materialManager = services.initializeMaterialManager();
    auto &scene = services.initializeScene();

    renderer::RendererConfig rendererConfig{};
    rendererConfig.globalResources.meshFiles.push_back("assets/car/car.obj");
    rendererConfig.globalResources.textureFiles.push_back("assets/car/texture_pbr_20250901.png");
    rendererConfig.globalResources.textureFiles.push_back("assets/car/texture_pbr_20250901_metallic.png");
    rendererConfig.globalResources.textureFiles.push_back("assets/car/texture_pbr_20250901_roughness.png");
    rendererConfig.globalResources.textureFiles.push_back("assets/car/texture_pbr_20250901_normal.png");
    rendererConfig.globalResources.shaders.push_back({"assets/shaders/spv", "car", false});
    rendererConfig.frameDefinition.shaderPrefix = "car";
    rendererConfig.swapchainAttachmentName = "Swapchain";

    renderer::RenderPassDefinition mainPass{};
    mainPass.name = "MainPass";
    mainPass.shaderPrefix = "car";
    mainPass.renderExtent = context.getSwapchainExtent();

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

    auto meshId = resourceManager.loadMesh("assets/car/car.obj");
    auto materialId = materialManager.loadMaterialFromJson("assets/car/car.json");
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
        auto vtxToken = transferManager.uploadToBuffer(gpuMesh.vertexBuffer, mesh.vertices.data(), vtxDesc.size);

        vkcore::BufferDesc idxDesc{};
        idxDesc.size = mesh.getIndexDataSize();
        idxDesc.usage = vkcore::BufferUsageFlags::Index | vkcore::BufferUsageFlags::TransferDst;
        idxDesc.memory = vkcore::MemoryUsage::GpuOnly;
        idxDesc.debugName = "CarIndexBuffer";
        gpuMesh.indexBuffer = allocator.createBuffer(idxDesc);
        auto idxToken = transferManager.uploadToBuffer(gpuMesh.indexBuffer, mesh.indices.data(), idxDesc.size);

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
                                             : std::string("assets/car/texture_pbr_20250901.png"));

    renderer.registerPassCallback("MainPass", [&](const renderer::RenderPassDefinition &,
                                                  const renderer::PassDrawContext &ctx) {
        vk::DescriptorSet cameraSet = ctx.frameResources.descriptorSets.size() > 0 ? ctx.frameResources.descriptorSets[0]
                                                                                   : vk::DescriptorSet{};
        vk::DescriptorSet materialSet = ctx.frameResources.descriptorSets.size() > 1
                                             ? ctx.frameResources.descriptorSets[1]
                                             : vk::DescriptorSet{};

        auto device = context.getDevice();
        if (ctx.frameResources.descriptorSchemas.size() > 0 && cameraSet)
        {
            auto writer = vkcore::DescriptorSetWriter::begin(device, ctx.frameResources.descriptorSchemas[0], cameraSet);
            writer.writeBuffer("CameraData", ctx.frameResources.cameraBuffer)
                .writeBuffer("LightData", ctx.frameResources.lightBuffer)
                .update();
        }

        if (ctx.frameResources.descriptorSchemas.size() > 1 && materialSet)
        {
            auto writer = vkcore::DescriptorSetWriter::begin(device, ctx.frameResources.descriptorSchemas[1], materialSet);
            writer.writeSampledImage("baseColorTex", baseColor.image, baseColor.sampler,
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