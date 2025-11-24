#pragma once

#include "SceneTypes.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace asset
{

/**
 * @brief 管理并遍历场景内容的容器。
 *
 * Scene 负责创建节点、维护活动相机，并提供遍历接口与 UBO 构建工具，
 * 以便渲染管线直接消费场景数据。
 */
class Scene
{
  public:
    Scene();

    /**
     * @brief 创建携带相机组件的节点。
     * @param camera 相机参数。
     * @return 新创建的节点引用，调用者可继续修改其他组件。
     */
    SceneNode &createCameraNode(const Camera &camera);

    /**
     * @brief 创建携带光源组件的节点。
     * @param light 光源描述。
     * @return 新创建的节点引用。
     */
    SceneNode &createLightNode(const Light &light);

    /**
     * @brief 创建携带可渲染组件的节点。
     * @param renderable 网格与材质引用等渲染数据。
     * @return 新创建的节点引用。
     */
    SceneNode &createRenderableNode(const RenderableComponent &renderable);

    SceneNode *getNode(uint32_t id);
    const SceneNode *getNode(uint32_t id) const;

    /**
     * @brief 将指定节点设置为活动相机。
     * @param id 拥有相机组件的节点 ID。
     */
    void setActiveCamera(uint32_t id);
    const SceneNode *getActiveCamera() const;

    /**
     * @brief 遍历场景中的所有节点。
     */
    void forEachNode(const std::function<void(const SceneNode &)> &visitor) const;

    /**
     * @brief 遍历所有可渲染节点，并同时提供节点与组件数据。
     */
    void forEachRenderable(const std::function<void(const SceneNode &, const RenderableComponent &)> &visitor) const;

    /**
     * @brief 遍历所有光源节点，并同时提供节点与组件数据。
     */
    void forEachLight(const std::function<void(const SceneNode &, const Light &)> &visitor) const;

    /**
     * @brief 从指定相机节点生成 UBO。
     */
    [[nodiscard]] CameraUBO buildCameraUBO(const SceneNode &cameraNode) const;

    /**
     * @brief 基于当前活动相机构建 UBO。
     */
    [[nodiscard]] CameraUBO buildActiveCameraUBO() const;

    /**
     * @brief 收集场景中的光源信息并生成 UBO。
     */
    [[nodiscard]] LightUBO buildLightUBO() const;

    [[nodiscard]] const std::vector<SceneNode> &nodes() const;

  private:
    uint32_t m_nextId{1};
    uint32_t m_activeCameraId{0};
    std::vector<SceneNode> m_nodes{};

    SceneNode &createNode(SceneNodeType type);
};

} // namespace asset

