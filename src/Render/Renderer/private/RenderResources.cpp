#include "RenderResources.hpp"

#include <stdexcept>

namespace renderer
{

RendererResourceService::RendererResourceService(asset::ResourceManager &resourceManager,
                                                 vkcore::TransferManager &transferManager,
                                                 vkcore::VkResourceAllocator &allocator, vkcore::VkContext &context)
    : m_resourceManager(&resourceManager), m_transferManager(&transferManager), m_allocator(&allocator),
      m_context(&context)
{
}

void RendererResourceService::preloadGlobalResources(const RendererGlobalResources &resources)
{
    for (const auto &mesh : resources.meshFiles)
    {
        m_resourceManager->loadMesh(mesh);
    }

    for (const auto &texture : resources.textureFiles)
    {
        m_resourceManager->loadTexture(texture);
    }

    for (const auto &shader : resources.shaders)
    {
        m_resourceManager->loadShader(shader.directory, shader.name, shader.enableCompute);
    }
}

PerFrameGpuResources RendererResourceService::createPerFrameResources(const FrameResourceDefinition &definition)
{
    if (definition.framesInFlight == 0)
    {
        throw std::runtime_error("framesInFlight must be greater than zero");
    }

    PerFrameGpuResources resources;

    vkcore::BufferDesc cameraDesc{};
    cameraDesc.size = definition.cameraBufferSize;
    cameraDesc.usage = vkcore::BufferUsageFlags::Uniform | vkcore::BufferUsageFlags::TransferDst;
    cameraDesc.memory = vkcore::MemoryUsage::GpuOnly;
    cameraDesc.debugName = "CameraUBO";
    resources.cameraBuffer = m_allocator->createBuffer(cameraDesc);

    vkcore::BufferDesc lightDesc{};
    lightDesc.size = definition.lightBufferSize;
    lightDesc.usage = vkcore::BufferUsageFlags::Uniform | vkcore::BufferUsageFlags::TransferDst;
    lightDesc.memory = vkcore::MemoryUsage::GpuOnly;
    lightDesc.debugName = "LightUBO";
    resources.lightBuffer = m_allocator->createBuffer(lightDesc);

    resources.descriptorSchemas = m_resourceManager->getShaderDescriptorSchemas(definition.shaderPrefix);
    resources.descriptorSets =
        m_resourceManager->getOrAllocateDescriptorSet(resources.descriptorSchemas, definition.shaderPrefix);
    return resources;
}

vkcore::TransferToken RendererResourceService::uploadCameraData(const PerFrameGpuResources &frameResources,
                                                                const asset::CameraUBO &camera) const
{
    return m_transferManager->uploadToBuffer(frameResources.cameraBuffer, camera);
}

vkcore::TransferToken RendererResourceService::uploadLightData(const PerFrameGpuResources &frameResources,
                                                               const asset::LightUBO &lights) const
{
    return m_transferManager->uploadToBuffer(frameResources.lightBuffer, lights);
}

vkcore::DescriptorSetWriter RendererResourceService::beginDescriptorWrite(const PerFrameGpuResources &frameResources,
                                                                          uint32_t setIndex) const
{
    if (setIndex >= frameResources.descriptorSets.size())
    {
        throw std::out_of_range("Descriptor set index exceeds allocated sets");
    }

    auto schema =
        (setIndex < frameResources.descriptorSchemas.size()) ? frameResources.descriptorSchemas[setIndex] : nullptr;
    if (!schema)
    {
        throw std::runtime_error("Descriptor schema not available for requested set index");
    }

    return vkcore::DescriptorSetWriter::begin(m_context->getDevice(), schema, frameResources.descriptorSets[setIndex]);
}

} // namespace renderer
