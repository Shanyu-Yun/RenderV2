#pragma once

#include "ResourceType.hpp"
#include <glm/glm.hpp>
#include <string>

namespace asset
{

/**
 * @struct PBRMaterial
 * @brief 表示PBR材质的参数和关联的纹理资源ID
 */
struct PBRMaterial
{
    struct TextureIds
    {
        std::string baseColor;
        std::string metallic;
        std::string roughness;
        std::string normal;
        std::string occlusion;
        std::string emissive;
    } textures;

    struct Factors
    {
        glm::vec4 baseColor{1.0f};
        float metallic{1.0f};
        float roughness{1.0f};
        glm::vec3 emissive{0.0f};
        float normalScale{1.0f};
    } factors;

    struct Alpha
    {
        AlphaMode mode{AlphaMode::Opaque};
        float cutoff{0.5f};
        bool doubleSided{false};
    } alpha;

    struct Optical
    {
        float refractionIndex{1.0f};
    } optical;

    std::string name;
    std::string domain;
};

} // namespace asset
