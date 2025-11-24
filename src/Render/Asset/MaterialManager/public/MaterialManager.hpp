#pragma once

#include "Material.hpp"
#include "ResourceType.hpp"
#include <filesystem>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace asset
{

class ResourceManager;

/**
 * @class MaterialManager
 * @brief 负责加载和管理PBR材质，仅存储ResourceManager的资源ID
 */
class MaterialManager
{
  public:
    explicit MaterialManager(ResourceManager &resourceManager);

    /**
     * @brief 从JSON文件加载PBR材质
     * @param filepath 材质描述文件路径
     * @return 材质名称（作为ID）
     */
    std::string loadMaterialFromJson(const std::filesystem::path &filepath);

    /**
     * @brief 获取材质
     * @param name 材质名称
     * @return PBRMaterial智能指针，不存在时返回nullptr
     */
    std::shared_ptr<PBRMaterial> getMaterial(const std::string &name);

    /**
     * @brief 清空所有已加载材质
     */
    void clear();

  private:
    static glm::vec4 parseVec4(const nlohmann::json &j, const glm::vec4 &defaultValue);
    static glm::vec3 parseVec3(const nlohmann::json &j, const glm::vec3 &defaultValue);
    static AlphaMode parseAlphaMode(const std::string &modeStr);

    PBRMaterial parseMaterialJson(const std::filesystem::path &filepath, const nlohmann::json &materialJson);

  private:
    ResourceManager *m_resourceManager{nullptr};
    std::unordered_map<std::string, std::shared_ptr<PBRMaterial>> m_materials;
    std::mutex m_mutex;
};

} // namespace asset
