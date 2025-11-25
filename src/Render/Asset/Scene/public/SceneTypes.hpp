#pragma once

#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <string>

namespace asset
{

/**
 * @brief 描述一台相机的视图与投影参数。
 *
 * 该结构体只负责保存相机的原始参数，不参与场景的节点管理；
 * 其矩阵计算函数在实现文件中提供，便于统一维护数学逻辑。
 */
struct Camera
{
    /// 世界空间中的相机位置。
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    /// 相机观察的目标点。
    glm::vec3 target{0.0f, 0.0f, -1.0f};
    /// 世界空间中的相机上方向。
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    /// 纵向视场角（弧度）。
    float fovY{glm::radians(60.0f)};
    /// 视口宽高比（width / height）。
    float aspect{16.0f / 9.0f};
    /// 近平面距离。
    float nearClip{0.1f};
    /// 远平面距离。
    float farClip{1000.0f};

    [[nodiscard]] glm::mat4 viewMatrix() const;
    [[nodiscard]] glm::mat4 projectionMatrix() const;
};

/**
 * @brief 相机传入 GPU 时的统一布局。
 *
 * 该结构体使用列主序矩阵存储视图与投影信息，并包含视点位置，
 * 方便在光照或其他着色阶段直接读取。
 */
struct CameraUBO
{
    glm::mat4 view{1.0f};                           ///< 视图矩阵。
    glm::mat4 projection{1.0f};                     ///< 投影矩阵。
    glm::vec4 viewPosition{0.0f, 0.0f, 0.0f, 1.0f}; ///< 世界空间的相机位置，齐次坐标形式。
};

/**
 * @brief 支持的光源类型。
 */
enum class LightType : uint32_t
{
    Point = 0,
    Directional = 1,
    Spot = 2,
};

/**
 * @brief 场景中的光源描述数据。
 *
 * 不同类型的光源共用同一结构，未使用的字段可保持默认值。
 */
struct Light
{
    LightType type{LightType::Directional}; ///< 光源类型。
    glm::vec3 color{1.0f};                  ///< 线性空间的光颜色。
    float intensity{1.0f};                  ///< 光照强度，通常作为整体系数使用。

    // Directional 光源参数
    glm::vec3 direction{0.0f, -1.0f, 0.0f}; ///< 平行光方向，需为单位向量。

    // Point/Spot 光源参数
    glm::vec3 position{0.0f}; ///< 点光/聚光位置。
    float range{100.0f};      ///< 点光/聚光衰减范围。

    // Spot 光源参数（弧度）
    float innerCone{glm::radians(15.0f)}; ///< 聚光内锥角。
    float outerCone{glm::radians(25.0f)}; ///< 聚光外锥角。
};

/**
 * @brief 光源在 GPU 侧的布局描述。
 *
 * 该布局兼容常见的 Shader UniformBuffer 定义，包含固定数量的光源槽位。
 */
struct LightUBO
{
    struct GpuLight
    {
        glm::vec4 position{0.0f};       ///< xyz 为世界坐标，w 存储 range。
        glm::vec4 direction{0.0f};      ///< xyz 为单位方向，w 存储 light type。
        glm::vec4 colorIntensity{0.0f}; ///< rgb 为颜色，w 存储强度。
        glm::vec4 spotParams{0.0f};     ///< x 内锥角，y 外锥角，其余保留。
    };

    static constexpr size_t MaxLights = 16;   ///< 支持的最大光源数量。
    std::array<GpuLight, MaxLights> lights{}; ///< 固定大小的光源数组。
    uint32_t lightCount{0};                   ///< 当前有效光源数量。
    uint32_t pad[3];                          /// 对齐填充
};

/**
 * @brief 可渲染物体的基础组件。
 *
 * 包含渲染所需的 Mesh 与材质引用，并提供可见性开关。
 */
struct RenderableComponent
{
    std::string meshId;     ///< ResourceManager 提供的网格标识。
    std::string materialId; ///< MaterialManager 提供的材质标识。
    bool visible{true};     ///< 是否参与渲染提交。
};

/**
 * @brief 使用轴对齐包围盒描述的碰撞体组件。
 */
struct ColliderComponent
{
    glm::vec3 center{0.0f};      ///< AABB 中心位置。
    glm::vec3 halfExtents{0.5f}; ///< AABB 半径，表示到各轴正负方向的距离。
};

/**
 * @brief 基础变换组件，包含位移、旋转与缩放。
 */
struct TransformComponent
{
    glm::vec3 position{0.0f};                   ///< 世界空间位移。
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; ///< 世界空间旋转（四元数）。
    glm::vec3 scale{1.0f};                      ///< 逐轴缩放。

    /**
     * @brief 生成 TRS 变换矩阵。
     * @return 4x4 齐次坐标矩阵，按照平移 * 旋转 * 缩放顺序组合。
     */
    [[nodiscard]] glm::mat4 matrix() const;
};

/**
 * @brief 场景节点类型。
 */
enum class SceneNodeType
{
    Camera,     ///< 相机节点。
    Light,      ///< 光照节点。
    Renderable, ///< 可渲染节点。
};

/**
 * @brief 场景中的节点描述，可附加不同组件。
 *
 * 该结构只负责保存数据，实际的管理与遍历由 Scene 类完成。
 */
struct SceneNode
{
    uint32_t id{0};                                  ///< 节点唯一标识。
    SceneNodeType type{SceneNodeType::Renderable};   ///< 节点类别。
    TransformComponent transform{};                  ///< 基础变换组件。
    std::optional<ColliderComponent> collider{};     ///< 可选的碰撞体组件。
    std::optional<Camera> camera{};                  ///< 可选相机组件。
    std::optional<Light> light{};                    ///< 可选光源组件。
    std::optional<RenderableComponent> renderable{}; ///< 可选渲染组件。
};

} // namespace asset
