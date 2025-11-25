#include "Render/Renderer/public/EngineServices.hpp"
#include "Render/VkCore/public/Logger.hpp"
#include "Render/VkCore/public/vkcore.hpp"
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
    mainWindow.show();
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
    swapchainConfig.imageCount = 3;

    auto &context =
        services.initializeVkContext(instanceConfig, deviceConfig, reinterpret_cast<void *>(winId), swapchainConfig);
    services.initializeResourceAllocator();
    services.initializeTransferManager();
    auto &allocator = services.getService<vkcore::VkResourceAllocator>();
    auto &transferManager = services.getService<vkcore::TransferManager>();
    auto &resourceManager = services.initializeResourceManager();
    auto &materialManager = services.initializeMaterialManager();
    auto &scene = services.initializeScene();

    auto meshFuture = resourceManager.loadMeshAsync(carAssetRoot / "car.obj");
    auto shaderFuture = resourceManager.loadShaderAsync(shaderRoot, "car", false);

    //加载材质
    std::string materialId = materialManager.loadMaterialFromJson(carAssetRoot / "car.json");
    std::shared_ptr<asset::PBRMaterial> material = materialManager.getMaterial(materialId);
    std::vector<std::filesystem::path> texturePaths = {material->textures.baseColor, material->textures.metallic,
                                                       material->textures.roughness, material->textures.normal};

    std::shared_future<std::vector<std::string>> textureFutures = resourceManager.loadTexturesAsync(texturePaths);

    //创建采样器
    vkcore::ManagedSampler sampler =
        allocator.createSampler(vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear,
                                vk::SamplerAddressMode::eRepeat, 0.0f, "Material Sampler");

    // 等待资源加载完成
    auto meshName = meshFuture.get();
    auto shaderName = shaderFuture.get();
    std::vector<std::string> textureNames = textureFutures.get();

    // 创建渲染对象、相机和光源
    // 创建光源
    asset::Light directionalLight;
    directionalLight.type = asset::LightType::Directional;
    directionalLight.color = glm::vec3(1.0f);
    directionalLight.intensity = 3.0f;
    directionalLight.direction = glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f));

    asset::Light pointLight;
    pointLight.type = asset::LightType::Point;
    pointLight.color = glm::vec3(1.0f, 0.8f, 0.6f);
    pointLight.intensity = 50.0f;
    pointLight.position = glm::vec3(0.0f, 5.0f, 5.0f);
    pointLight.range = 20.0f;
    scene.createLightNode(directionalLight);
    scene.createLightNode(pointLight);

    //创建相机
    asset::Camera camera;
    camera.position = glm::vec3(0.0f, 2.0f, 10.0f);
    camera.target = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.fovY = glm::radians(60.0f);
    camera.nearClip = 0.1f;
    camera.farClip = 1000.0f;
    auto cameraNode = scene.createCameraNode(camera);

    // 创建渲染对象
    asset::RenderableComponent renderable;
    renderable.meshId = meshName;
    renderable.materialId = materialId;
    scene.createRenderableNode(renderable);

    std::vector<asset::RenderableComponent> renderables;
    auto visitor = [&](const asset::SceneNode &node, const asset::RenderableComponent &comp) {
        renderables.push_back(comp);
    };
    scene.forEachRenderable(visitor);

    std::shared_ptr<asset::PBRMaterial> RenderTextures = materialManager.getMaterial(materialId);

    //创建GPU资源
    //创建Camera UBO缓冲区
    vkcore::BufferDesc cameraBufferDesc;
    cameraBufferDesc.size = sizeof(asset::CameraUBO);
    cameraBufferDesc.usage = vkcore::BufferUsageFlags::Uniform | vkcore::BufferUsageFlags::StagingDst;
    cameraBufferDesc.memory = vkcore::MemoryUsage::GpuToCpu;
    cameraBufferDesc.debugName = "Camera UBO Buffer";
    auto cameraBuffer = allocator.createBuffer(cameraBufferDesc);

    //创建光照缓冲区
    vkcore::BufferDesc lightBufferDesc;
    lightBufferDesc.size = sizeof(asset::LightUBO);
    lightBufferDesc.usage = vkcore::BufferUsageFlags::Uniform | vkcore::BufferUsageFlags::StagingDst;
    lightBufferDesc.memory = vkcore::MemoryUsage::GpuToCpu;
    lightBufferDesc.debugName = "Light UBO Buffer";
    auto lightBuffer = allocator.createBuffer(lightBufferDesc);

    //创建顶点缓冲区
    vkcore::BufferDesc vertexBufferDesc;
    vertexBufferDesc.size = (*resourceManager.getMesh(meshName))[0].vertices.size() * sizeof(asset::Vertex);
    vertexBufferDesc.usage = vkcore::BufferUsageFlags::Vertex | vkcore::BufferUsageFlags::StagingDst;
    vertexBufferDesc.memory = vkcore::MemoryUsage::GpuOnly;
    vertexBufferDesc.debugName = "Vertex Buffer";
    auto vertexBuffer = allocator.createBuffer(vertexBufferDesc);

    //创建索引缓冲区
    vkcore::BufferDesc indexBufferDesc;
    indexBufferDesc.size = (*resourceManager.getMesh(meshName))[0].indices.size() * sizeof(uint32_t);
    indexBufferDesc.usage = vkcore::BufferUsageFlags::Index | vkcore::BufferUsageFlags::StagingDst;
    indexBufferDesc.memory = vkcore::MemoryUsage::GpuOnly;
    indexBufferDesc.debugName = "Index Buffer";
    auto indexBuffer = allocator.createBuffer(indexBufferDesc);

    //创建材质纹理Image
    std::vector<vkcore::ManagedImage> textureImages;
    for (const auto &texName : textureNames)
    {
        vkcore::ImageDesc imageDesc;
        imageDesc.debugName = "Texture Image: " + texName;
        imageDesc.width = resourceManager.getTexture(texName)->width;
        imageDesc.height = resourceManager.getTexture(texName)->height;
        imageDesc.depth = 1;
        imageDesc.mipLevels = 1;
        imageDesc.arrayLayers = 1;
        imageDesc.format = vk::Format::eR8G8B8A8Unorm;
        imageDesc.usage = vkcore::ImageUsageFlags::Sampled | vkcore::ImageUsageFlags::TransferDst;
        imageDesc.memory = vkcore::MemoryUsage::GpuOnly;
        imageDesc.type = vk::ImageType::e2D;
        imageDesc.tiling = vk::ImageTiling::eOptimal;
        imageDesc.flags = {};
        auto textureImage = allocator.createImage(imageDesc);
        textureImages.push_back(std::move(textureImage));
    }

    //上传GPU资源数据
    //上传Buffers数据
    auto cameraUBO = scene.buildCameraUBO(cameraNode);
    transferManager.writeToUniformBuffer(cameraBuffer, &cameraUBO, sizeof(asset::CameraUBO), 0);
    auto lightUBO = scene.buildLightUBO();
    transferManager.writeToUniformBuffer(lightBuffer, &lightUBO, sizeof(asset::LightUBO), 0);

    auto vertexToken =
        transferManager.uploadToBuffer(vertexBuffer, (*resourceManager.getMesh(meshName))[0].vertices, 0);
    auto indexToken = transferManager.uploadToBuffer(indexBuffer, (*resourceManager.getMesh(meshName))[0].indices, 0);

    //上传纹理数据
    std::vector<vkcore::TransferToken> textureTokens;
    for (size_t i = 0; i < textureNames.size(); ++i)
    {
        auto textureData = resourceManager.getTexture(textureNames[i]);
        textureTokens.push_back(transferManager.uploadToImage(textureImages[i], textureData->pixels,
                                                              textureData->dataSize, textureData->width,
                                                              textureData->height, 1, 0, 0));
    }
    //等待上传完成
    for (auto &token : textureTokens)
    {
        if (!token.isComplete())
        {
            token.wait();
        }
    }

    if (!vertexToken.isComplete())
    {
        vertexToken.wait();
    }
    if (!indexToken.isComplete())
    {
        indexToken.wait();
    }
    //获取描述符集布局和分配描述符
    std::vector<std::shared_ptr<const vkcore::DescriptorSetSchema>> descriptorSetSchemas =
        resourceManager.getShaderDescriptorSchemas("car"); //注意是着色器的名称不带路径信息
    std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
    for (const auto &schema : descriptorSetSchemas)
    {
        descriptorSetLayouts.push_back(schema->getLayout());
    }

    std::vector<vk::DescriptorSet> descriptorSets =
        resourceManager.getOrAllocateDescriptorSet(descriptorSetSchemas, shaderName);

    //更新描述符集
    vkcore::DescriptorSetWriter::begin(context.getDevice(), descriptorSetSchemas[0], descriptorSets[0])
        .writeBuffer("uLights", vk::DescriptorBufferInfo{lightBuffer.getBuffer(), 0, sizeof(asset::LightUBO)})
        .writeBuffer("uCamera", vk::DescriptorBufferInfo{cameraBuffer.getBuffer(), 0, sizeof(asset::CameraUBO)})
        .update();

    vkcore::DescriptorSetWriter::begin(context.getDevice(), descriptorSetSchemas[1], descriptorSets[1])
        .writeImage("uBaseColorMap", vk::DescriptorImageInfo{sampler.getSampler(), textureImages[0].getView(),
                                                             vk::ImageLayout::eShaderReadOnlyOptimal})
        .writeImage("uMetallicMap", vk::DescriptorImageInfo{sampler.getSampler(), textureImages[1].getView(),
                                                            vk::ImageLayout::eShaderReadOnlyOptimal})
        .writeImage("uRoughnessMap", vk::DescriptorImageInfo{sampler.getSampler(), textureImages[2].getView(),
                                                             vk::ImageLayout::eShaderReadOnlyOptimal})
        .writeImage("uNormalMap", vk::DescriptorImageInfo{sampler.getSampler(), textureImages[3].getView(),
                                                          vk::ImageLayout::eShaderReadOnlyOptimal})
        .update();

    //创建交换链Image的视图
    std::vector<vk::ImageView> swapchainImageViews;
    swapchainImageViews.reserve(context.getSwapchainImages().size());
    for (const auto &image : context.getSwapchainImages())
    {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = context.getSwapchainImageFormat();
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        vk::ImageView imageView = context.getDevice().createImageView(viewInfo);
        swapchainImageViews.push_back(imageView);
    }
    //创建深度附件
    vkcore::ImageDesc depthAttachmentDesc;
    depthAttachmentDesc.debugName = "Depth Attachment";
    depthAttachmentDesc.width = context.getSwapchainExtent().width;
    depthAttachmentDesc.height = context.getSwapchainExtent().height;
    depthAttachmentDesc.depth = 1;
    depthAttachmentDesc.mipLevels = 1;
    depthAttachmentDesc.arrayLayers = 1;
    depthAttachmentDesc.format = vk::Format::eD32Sfloat;
    depthAttachmentDesc.usage = vkcore::ImageUsageFlags::DepthStencil;
    depthAttachmentDesc.memory = vkcore::MemoryUsage::GpuOnly;
    depthAttachmentDesc.type = vk::ImageType::e2D;
    depthAttachmentDesc.tiling = vk::ImageTiling::eOptimal;
    depthAttachmentDesc.flags = {};
    auto depthAttachment = allocator.createImage(depthAttachmentDesc, vk::ImageAspectFlagBits::eDepth);

    //创建图形管线基于动态渲染
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.stage = vk::ShaderStageFlagBits::eVertex;
    vertShaderStageInfo.module = resourceManager.getShaderprogram(shaderName).vertexShader->shaderModule;
    vertShaderStageInfo.pName = "main";
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.stage = vk::ShaderStageFlagBits::eFragment;
    fragShaderStageInfo.module = resourceManager.getShaderprogram(shaderName).fragmentShader->shaderModule;
    fragShaderStageInfo.pName = "main";
    vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    //顶点输入状态
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};
    auto bindingDescription = asset::Vertex::getBindingDescription();
    auto attributeDescriptions = asset::Vertex::getAttributeDescriptions();

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // input汇编状态
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 视口和裁剪矩形（动态状态）
    vk::PipelineViewportStateCreateInfo viewportState{};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    std::array dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};

    // 动态状态创建信息
    vk::PipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // 光栅化状态
    vk::PipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;

    // 多重采样状态
    vk::PipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
    // 深度模板状态
    vk::PipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 颜色混合状态
    vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = VK_FALSE;
    vk::PipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 管线布局
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
    vk::PipelineLayout pipelineLayout = context.getDevice().createPipelineLayout(pipelineLayoutInfo);

    // 动态渲染管线创建
    vk::PipelineRenderingCreateInfo pipelineRenderingInfo{};
    pipelineRenderingInfo.colorAttachmentCount = 1;
    vk::Format swapchainFormat = context.getSwapchainImageFormat();
    pipelineRenderingInfo.pColorAttachmentFormats = &swapchainFormat;
    pipelineRenderingInfo.depthAttachmentFormat = vk::Format::eD32Sfloat;
    pipelineRenderingInfo.stencilAttachmentFormat = vk::Format::eUndefined;

    //创建图形管线
    vk::GraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = nullptr; // 使用动态渲染，不需要预定义的渲染通道
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.pNext = &pipelineRenderingInfo;

    vk::Pipeline graphicsPipeline = context.getDevice().createGraphicsPipeline(VK_NULL_HANDLE, pipelineInfo).value;

    //创建命令缓冲区池和命令缓冲区
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.queueFamilyIndex = context.getQueueFamilyIndices().graphicsFamily.value();
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    vk::CommandPool commandPool = context.getDevice().createCommandPool(poolInfo);

    std::vector<vk::CommandBuffer> commandBuffers;
    commandBuffers.resize(context.getSwapchainImages().size());

    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.commandPool = commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
    commandBuffers = context.getDevice().allocateCommandBuffers(allocInfo);

    //主渲染循环
    int frameIndex = 0;
    const uint32_t maxFramesInFlight = 3;

    std::vector<vk::Semaphore> imageAvailableSemaphores(maxFramesInFlight);
    std::vector<vk::Semaphore> renderFinishedSemaphores(maxFramesInFlight);
    std::vector<vk::Fence> inFlightFences(maxFramesInFlight);

    vk::SemaphoreCreateInfo semInfo{};
    vk::FenceCreateInfo fenceInfo{};
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled; // 第一帧不需要等待

    for (size_t i = 0; i < maxFramesInFlight; ++i)
    {
        imageAvailableSemaphores[i] = context.getDevice().createSemaphore(semInfo);
        renderFinishedSemaphores[i] = context.getDevice().createSemaphore(semInfo);
        inFlightFences[i] = context.getDevice().createFence(fenceInfo);
    }

    bool depthInitialized = false;
    const uint32_t maxFrames = 1000;
    frameIndex = 0;

    for (uint32_t frame = 0; frame < maxFrames; ++frame)
    {
        uint32_t currentFrame = frameIndex % maxFramesInFlight;

        context.getDevice().waitForFences(1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
        context.getDevice().resetFences(1, &inFlightFences[currentFrame]);

        auto acquireResult = context.getDevice().acquireNextImageKHR(
            context.getSwapchain(), UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE);

        if (acquireResult.result != vk::Result::eSuccess)
        {
            std::cerr << "Failed to acquire swapchain image!" << std::endl;
            continue;
        }

        uint32_t imageIndex = acquireResult.value;
        vk::CommandBuffer commandBuffer = commandBuffers[imageIndex];
        commandBuffer.reset({});

        // 记录命令缓冲区
        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        commandBuffer.begin(beginInfo);

        //交换链颜色图像屏障
        {
            vk::ImageMemoryBarrier swapchainBarrier{};
            swapchainBarrier.oldLayout = vk::ImageLayout::eUndefined;
            swapchainBarrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
            swapchainBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            swapchainBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            swapchainBarrier.image = context.getSwapchainImages()[imageIndex];
            swapchainBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            swapchainBarrier.subresourceRange.baseMipLevel = 0;
            swapchainBarrier.subresourceRange.levelCount = 1;
            swapchainBarrier.subresourceRange.baseArrayLayer = 0;
            swapchainBarrier.subresourceRange.layerCount = 1;
            swapchainBarrier.srcAccessMask = {};
            swapchainBarrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                          vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, {},
                                          swapchainBarrier);
        }

        //深度图像屏障
        if (!depthInitialized)
        {
            vk::ImageMemoryBarrier depthBarrier{};
            depthBarrier.oldLayout = vk::ImageLayout::eUndefined;
            depthBarrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            depthBarrier.image = depthAttachment.getImage();
            depthBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
            depthBarrier.subresourceRange.baseMipLevel = 0;
            depthBarrier.subresourceRange.levelCount = 1;
            depthBarrier.subresourceRange.baseArrayLayer = 0;
            depthBarrier.subresourceRange.layerCount = 1;
            depthBarrier.srcAccessMask = {};
            depthBarrier.dstAccessMask =
                vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                          vk::PipelineStageFlagBits::eEarlyFragmentTests, {}, {}, {}, depthBarrier);
            depthInitialized = true;
        }

        //设置动态视口和裁剪矩形
        vk::Viewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(context.getSwapchainExtent().width);
        viewport.height = static_cast<float>(context.getSwapchainExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vk::Rect2D scissor{};
        scissor.offset = vk::Offset2D{0, 0};
        scissor.extent = context.getSwapchainExtent();
        commandBuffer.setViewport(0, 1, &viewport);
        commandBuffer.setScissor(0, 1, &scissor);

        //使用动态渲染开始渲染通道
        vk::RenderingAttachmentInfo colorAttachmentInfo{};
        colorAttachmentInfo.imageView = swapchainImageViews[imageIndex];
        colorAttachmentInfo.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        colorAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
        colorAttachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
        colorAttachmentInfo.clearValue.color = vk::ClearColorValue(std::array<float, 4>{0.1f, 0.1f, 0.1f, 1.0f});

        vk::RenderingAttachmentInfo depthAttachmentInfo{};
        depthAttachmentInfo.imageView = depthAttachment.getView();
        depthAttachmentInfo.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachmentInfo.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachmentInfo.storeOp = vk::AttachmentStoreOp::eStore;
        depthAttachmentInfo.clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

        vk::RenderingInfo renderingInfo{};
        renderingInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderingInfo.renderArea.extent = context.getSwapchainExtent();
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &colorAttachmentInfo;
        renderingInfo.pDepthAttachment = &depthAttachmentInfo;
        renderingInfo.pStencilAttachment = nullptr;

        commandBuffer.beginRendering(renderingInfo);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, graphicsPipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0,
                                         static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data(), 0,
                                         nullptr);
        auto renderVertex = vertexBuffer.getBuffer();
        vk::DeviceSize offsets[] = {0};
        commandBuffer.bindVertexBuffers(0, 1, &renderVertex, offsets);
        commandBuffer.bindIndexBuffer(indexBuffer.getBuffer(), 0, vk::IndexType::eUint32);
        commandBuffer.drawIndexed(static_cast<uint32_t>((*resourceManager.getMesh(meshName))[0].indices.size()), 1, 0,
                                  0, 0);
        commandBuffer.endRendering();

        //交换链图像屏障，准备呈现
        {
            vk::ImageMemoryBarrier swapchainBarrier{};
            swapchainBarrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
            swapchainBarrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
            swapchainBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            swapchainBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            swapchainBarrier.image = context.getSwapchainImages()[imageIndex];
            swapchainBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            swapchainBarrier.subresourceRange.baseMipLevel = 0;
            swapchainBarrier.subresourceRange.levelCount = 1;
            swapchainBarrier.subresourceRange.baseArrayLayer = 0;
            swapchainBarrier.subresourceRange.layerCount = 1;
            swapchainBarrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            swapchainBarrier.dstAccessMask = {};

            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                                          vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {}, swapchainBarrier);
        }
        commandBuffer.end();

        // 提交命令缓冲区
        vk::SubmitInfo submitInfo{};
        vk::Semaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
        vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
        vk::Semaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};

        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;
        context.getGraphicsQueue().submit(1, &submitInfo, inFlightFences[currentFrame]);

        // 提交呈现请求
        vk::PresentInfoKHR presentInfo{};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        vk::SwapchainKHR swapchains[] = {context.getSwapchain()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;
        context.getPresentQueue().presentKHR(presentInfo);

        frameIndex = frameIndex + 1;
    }

    return app.exec();
}