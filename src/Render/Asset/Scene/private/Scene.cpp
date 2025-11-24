#include "Scene.hpp"

#include <algorithm>
#include <glm/ext/matrix_clip_space.hpp>

namespace asset
{

glm::mat4 Camera::viewMatrix() const
{
    return glm::lookAt(position, target, up);
}

glm::mat4 Camera::projectionMatrix() const
{
    // Vulkan 使用 0..1 的深度范围，需采用 ZO 变体生成投影矩阵。
    return glm::perspectiveRH_ZO(fovY, aspect, nearClip, farClip);
}

glm::mat4 TransformComponent::matrix() const
{
    glm::mat4 translation = glm::translate(glm::mat4{1.0f}, position);
    glm::mat4 rotationMat = glm::mat4_cast(rotation);
    glm::mat4 scaleMat = glm::scale(glm::mat4{1.0f}, scale);
    return translation * rotationMat * scaleMat;
}

Scene::Scene() = default;

SceneNode &Scene::createNode(SceneNodeType type)
{
    SceneNode node{};
    node.id = m_nextId++;
    node.type = type;
    m_nodes.emplace_back(std::move(node));
    return m_nodes.back();
}

SceneNode &Scene::createCameraNode(const Camera &camera)
{
    SceneNode &node = createNode(SceneNodeType::Camera);
    node.camera = camera;
    node.transform.position = camera.position;
    if (m_activeCameraId == 0)
    {
        m_activeCameraId = node.id;
    }
    return node;
}

SceneNode &Scene::createLightNode(const Light &light)
{
    SceneNode &node = createNode(SceneNodeType::Light);
    node.light = light;
    node.transform.position = light.position;
    return node;
}

SceneNode &Scene::createRenderableNode(const RenderableComponent &renderable)
{
    SceneNode &node = createNode(SceneNodeType::Renderable);
    node.renderable = renderable;
    return node;
}

SceneNode *Scene::getNode(uint32_t id)
{
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [id](const SceneNode &n) { return n.id == id; });
    if (it == m_nodes.end())
    {
        return nullptr;
    }
    return &(*it);
}

const SceneNode *Scene::getNode(uint32_t id) const
{
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [id](const SceneNode &n) { return n.id == id; });
    if (it == m_nodes.end())
    {
        return nullptr;
    }
    return &(*it);
}

void Scene::setActiveCamera(uint32_t id)
{
    const SceneNode *node = getNode(id);
    if (node != nullptr && node->camera.has_value())
    {
        m_activeCameraId = id;
    }
}

const SceneNode *Scene::getActiveCamera() const
{
    if (m_activeCameraId == 0)
    {
        return nullptr;
    }
    return getNode(m_activeCameraId);
}

void Scene::forEachNode(const std::function<void(const SceneNode &)> &visitor) const
{
    for (const auto &node : m_nodes)
    {
        visitor(node);
    }
}

void Scene::forEachRenderable(const std::function<void(const SceneNode &, const RenderableComponent &)> &visitor) const
{
    for (const auto &node : m_nodes)
    {
        if (node.renderable.has_value())
        {
            visitor(node, *node.renderable);
        }
    }
}

void Scene::forEachLight(const std::function<void(const SceneNode &, const Light &)> &visitor) const
{
    for (const auto &node : m_nodes)
    {
        if (node.light.has_value())
        {
            visitor(node, *node.light);
        }
    }
}

CameraUBO Scene::buildCameraUBO(const SceneNode &cameraNode) const
{
    CameraUBO ubo{};
    if (cameraNode.camera.has_value())
    {
        const Camera &camera = *cameraNode.camera;
        ubo.view = camera.viewMatrix();
        ubo.projection = camera.projectionMatrix();
        ubo.viewPosition = glm::vec4(camera.position, 1.0f);
    }
    return ubo;
}

CameraUBO Scene::buildActiveCameraUBO() const
{
    const SceneNode *node = getActiveCamera();
    if (node == nullptr)
    {
        return {};
    }
    return buildCameraUBO(*node);
}

LightUBO Scene::buildLightUBO() const
{
    LightUBO ubo{};
    uint32_t index = 0;
    forEachLight([&ubo, &index](const SceneNode &node, const Light &light) {
        if (index >= LightUBO::MaxLights)
        {
            return;
        }

        LightUBO::GpuLight &gpuLight = ubo.lights[index];
        gpuLight.position = glm::vec4(node.transform.position, light.range);
        gpuLight.direction = glm::vec4(glm::normalize(light.direction), static_cast<float>(light.type));
        gpuLight.colorIntensity = glm::vec4(light.color, light.intensity);
        gpuLight.spotParams = glm::vec4(light.innerCone, light.outerCone, 0.0f, 0.0f);
        ++index;
    });
    ubo.lightCount = index;
    return ubo;
}

const std::vector<SceneNode> &Scene::nodes() const
{
    return m_nodes;
}

} // namespace asset

