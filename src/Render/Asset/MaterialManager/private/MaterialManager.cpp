#include "MaterialManager.hpp"
#include "ResourceManager.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace asset
{
MaterialManager::MaterialManager(ResourceManager &resourceManager) : m_resourceManager(&resourceManager)
{
}

std::string MaterialManager::loadMaterialFromJson(const std::filesystem::path &filepath)
{
    if (!std::filesystem::exists(filepath))
    {
        throw std::runtime_error("Material json file does not exist: " + filepath.string());
    }

    std::ifstream file(filepath);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open material json file: " + filepath.string());
    }

    nlohmann::json materialJson;
    file >> materialJson;

    PBRMaterial material = parseMaterialJson(filepath, materialJson);
    const std::string materialId = material.name.empty() ? filepath.stem().string() : material.name;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto iter = m_materials.find(materialId);
        if (iter != m_materials.end())
        {
            *(iter->second) = std::move(material);
            return materialId;
        }
        m_materials.emplace(materialId, std::make_shared<PBRMaterial>(std::move(material)));
    }

    return materialId;
}

std::shared_ptr<PBRMaterial> MaterialManager::getMaterial(const std::string &name)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto iter = m_materials.find(name);
    if (iter != m_materials.end())
    {
        return iter->second;
    }
    return nullptr;
}

void MaterialManager::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_materials.clear();
}

PBRMaterial MaterialManager::parseMaterialJson(const std::filesystem::path &filepath,
                                               const nlohmann::json &materialJson)
{
    if (!m_resourceManager)
    {
        throw std::runtime_error("MaterialManager requires a valid ResourceManager");
    }

    PBRMaterial material;
    const auto baseDir = filepath.parent_path();

    material.name = materialJson.value("name", filepath.stem().string());
    material.domain = materialJson.value("domain", std::string("Opaque"));

    // Textures
    if (materialJson.contains("textures"))
    {
        const auto &textures = materialJson.at("textures");
        auto loadIfPresent = [&](const char *key, std::string &target) {
            if (textures.contains(key))
            {
                auto texName = textures.at(key).get<std::string>();
                if (!texName.empty())
                {
                    auto texPath = baseDir / texName;
                    target = m_resourceManager->loadTexture(texPath);
                }
            }
        };
        loadIfPresent("baseColor", material.textures.baseColor);
        loadIfPresent("metallic", material.textures.metallic);
        loadIfPresent("roughness", material.textures.roughness);
        loadIfPresent("normal", material.textures.normal);
        loadIfPresent("occlusion", material.textures.occlusion);
        loadIfPresent("emissive", material.textures.emissive);
    }

    // Factors
    if (materialJson.contains("factors"))
    {
        const auto &factors = materialJson.at("factors");
        if (factors.contains("baseColor"))
        {
            material.factors.baseColor = parseVec4(factors.at("baseColor"), material.factors.baseColor);
        }
        material.factors.metallic = factors.value("metallic", material.factors.metallic);
        material.factors.roughness = factors.value("roughness", material.factors.roughness);
        if (factors.contains("emissive"))
        {
            material.factors.emissive = parseVec3(factors.at("emissive"), material.factors.emissive);
        }
        material.factors.normalScale = factors.value("normalScale", material.factors.normalScale);
    }

    // Alpha
    if (materialJson.contains("alpha"))
    {
        const auto &alpha = materialJson.at("alpha");
        if (alpha.contains("mode"))
        {
            material.alpha.mode = parseAlphaMode(alpha.at("mode").get<std::string>());
        }
        material.alpha.cutoff = alpha.value("cutoff", material.alpha.cutoff);
        material.alpha.doubleSided = alpha.value("doubleSided", material.alpha.doubleSided);
    }

    if (materialJson.contains("optical"))
    {
        const auto &optical = materialJson.at("optical");
        material.optical.refractionIndex = optical.value("refractionIndex", material.optical.refractionIndex);
    }

    return material;
}

glm::vec4 MaterialManager::parseVec4(const nlohmann::json &j, const glm::vec4 &defaultValue)
{
    if (!j.is_array() || j.size() != 4)
    {
        return defaultValue;
    }
    glm::vec4 value = defaultValue;
    for (size_t i = 0; i < 4; ++i)
    {
        value[static_cast<int>(i)] = j.at(i).get<float>();
    }
    return value;
}

glm::vec3 MaterialManager::parseVec3(const nlohmann::json &j, const glm::vec3 &defaultValue)
{
    if (!j.is_array() || j.size() != 3)
    {
        return defaultValue;
    }
    glm::vec3 value = defaultValue;
    for (size_t i = 0; i < 3; ++i)
    {
        value[static_cast<int>(i)] = j.at(i).get<float>();
    }
    return value;
}

AlphaMode MaterialManager::parseAlphaMode(const std::string &modeStr)
{
    if (modeStr == "Opaque" || modeStr == "opaque")
    {
        return AlphaMode::Opaque;
    }
    if (modeStr == "Mask" || modeStr == "mask")
    {
        return AlphaMode::Mask;
    }
    if (modeStr == "Blend" || modeStr == "blend")
    {
        return AlphaMode::Blend;
    }
    return AlphaMode::Opaque;
}

} // namespace asset
