#pragma once

#include <glm/glm.hpp>

namespace rendercore
{

/**
 * @struct CameraUBO
 * @brief 相机 Uniform Buffer 数据结构
 * @details 包含视图矩阵、投影矩阵和相机位置，用于顶点变换和片段着色
 */
struct CameraUBO
{
    alignas(16) glm::mat4 view;       ///< 视图矩阵
    alignas(16) glm::mat4 projection; ///< 投影矩阵
    alignas(16) glm::mat4 viewProj;   ///< 视图投影矩阵 (优化：预计算)
    alignas(16) glm::vec3 position;   ///< 相机世界空间位置
    float padding1;                   ///< 对齐填充
};

/**
 * @struct DirectionalLightData
 * @brief 平行光数据结构
 */
struct DirectionalLightData
{
    alignas(16) glm::vec3 direction; ///< 光照方向
    float intensity;                 ///< 光照强度
    alignas(16) glm::vec3 color;     ///< 光照颜色
    float padding1;                  ///< 对齐填充
};

/**
 * @struct PointLightData
 * @brief 点光源数据结构
 */
struct PointLightData
{
    alignas(16) glm::vec3 position; ///< 光源位置
    float intensity;                ///< 光照强度
    alignas(16) glm::vec3 color;    ///< 光照颜色
    float constant;                 ///< 常数衰减项
    float linear;                   ///< 线性衰减项
    float quadratic;                ///< 二次衰减项
    float padding1;                 ///< 对齐填充
    float padding2;                 ///< 对齐填充
};

/**
 * @struct SpotLightData
 * @brief 聚光灯数据结构
 */
struct SpotLightData
{
    alignas(16) glm::vec3 position;  ///< 光源位置
    float intensity;                 ///< 光照强度
    alignas(16) glm::vec3 direction; ///< 光照方向
    float innerCutoff;               ///< 内锥角余弦值
    alignas(16) glm::vec3 color;     ///< 光照颜色
    float outerCutoff;               ///< 外锥角余弦值
    float constant;                  ///< 常数衰减项
    float linear;                    ///< 线性衰减项
    float quadratic;                 ///< 二次衰减项
    float padding1;                  ///< 对齐填充
};

/**
 * @struct LightUBO
 * @brief 灯光 Uniform Buffer 数据结构
 * @details 包含场景中所有灯光的数据，支持最多 4 个平行光、8 个点光源和 4 个聚光灯
 */
struct LightUBO
{
    DirectionalLightData directionalLights[4]; ///< 平行光数组
    PointLightData pointLights[8];             ///< 点光源数组
    SpotLightData spotLights[4];               ///< 聚光灯数组

    alignas(16) glm::ivec4 lightCounts; ///< x: 平行光数量, y: 点光源数量, z: 聚光灯数量, w: 保留
};

} // namespace rendercore
