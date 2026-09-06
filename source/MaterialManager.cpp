#include "MaterialManager.h"
#include "neuLog.h"
#include <json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace neurender {

MaterialManager& MaterialManager::Instance() {
    static MaterialManager s_Instance;
    return s_Instance;
}

void MaterialManager::Init() {
    SetupCornellBoxDefaults();
}

void MaterialManager::SetupCornellBoxDefaults() {
    m_Materials.clear();

    // 0: 白灰漫反射（地面、顶面、后墙）
    Material matWhite;
    matWhite.name = "Cornell White";
    matWhite.type = MaterialType::Diffuse;
    matWhite.albedo = glm::vec3(0.725f, 0.71f, 0.68f);
    matWhite.roughness = 0.5f;
    AddMaterial(matWhite);

    // 1: 经典红色漫反射（左墙）
    Material matRed;
    matRed.name = "Cornell Red";
    matRed.type = MaterialType::Diffuse;
    matRed.albedo = glm::vec3(0.63f, 0.065f, 0.05f);
    matRed.roughness = 0.5f;
    AddMaterial(matRed);

    // 2: 经典绿色漫反射（右墙）
    Material matGreen;
    matGreen.name = "Cornell Green";
    matGreen.type = MaterialType::Diffuse;
    matGreen.albedo = glm::vec3(0.14f, 0.45f, 0.091f);
    matGreen.roughness = 0.5f;
    AddMaterial(matGreen);

    // 3: 顶灯面光源（emmision）
    Material matLight;
    matLight.name = "Ceiling Light";
    matLight.type = MaterialType::Emissive;
    matLight.albedo = glm::vec3(1.0f);
    matLight.emission = glm::vec3(1.0f, 0.95f, 0.85f);
    matLight.emissionIntensity = 15.0f;
    AddMaterial(matLight);

    // 4: 猴头（黄金高光金属）
    Material matGold;
    matGold.name = "Gold (Monkey)";
    matGold.type = MaterialType::Metal;
    matGold.albedo = glm::vec3(1.0f, 0.766f, 0.336f);
    matGold.metallic = 0.95f;
    matGold.roughness = 0.12f;
    AddMaterial(matGold);

    // 5: 棱角球（光学玻璃透光折射）
    Material matGlass;
    matGlass.name = "Glass (Sphere)";
    matGlass.type = MaterialType::Glass;
    matGlass.albedo = glm::vec3(1.0f, 1.0f, 1.0f);
    matGlass.ior = 1.52f;
    matGlass.transmission = 1.0f;
    matGlass.roughness = 0.0f;
    AddMaterial(matGlass);

    // 6: 长方体盒子
    Material matBox;
    matBox.name = "Box Material";
    matBox.type = MaterialType::Diffuse;
    matBox.albedo = glm::vec3(0.725f, 0.71f, 0.68f);
    matBox.roughness = 0.4f;
    AddMaterial(matBox);

    // 7: 锥体（蓝色金属质感）
    Material matCone;
    matCone.name = "Blue Metal (Cone)";
    matCone.type = MaterialType::Metal;
    matCone.albedo = glm::vec3(0.2f, 0.45f, 0.9f);
    matCone.metallic = 0.85f;
    matCone.roughness = 0.2f;
    AddMaterial(matCone);

    m_SelectedIndex = 0;
    m_Dirty = true;
    LOG_I("Setup default Cornell Box materials ({} materials registered).", m_Materials.size());
}

int MaterialManager::AddMaterial(const Material& mat) {
    m_Materials.push_back(mat);
    m_Dirty = true;
    return static_cast<int>(m_Materials.size() - 1);
}

Material* MaterialManager::GetMaterial(int id) {
    if (id >= 0 && id < static_cast<int>(m_Materials.size())) {
        return &m_Materials[id];
    }
    return nullptr;
}

const Material* MaterialManager::GetMaterial(int id) const {
    if (id >= 0 && id < static_cast<int>(m_Materials.size())) {
        return &m_Materials[id];
    }
    return nullptr;
}

int MaterialManager::FindMaterial(const std::string& name) const {
    for (size_t i = 0; i < m_Materials.size(); ++i) {
        if (m_Materials[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MaterialManager::SetMaterial(int id, const Material& mat) {
    if (id >= 0 && id < static_cast<int>(m_Materials.size())) {
        m_Materials[id] = mat;
        m_Dirty = true;
    }
}

void MaterialManager::RemoveMaterial(int id) {
    if (id >= 0 && id < static_cast<int>(m_Materials.size())) {
        m_Materials.erase(m_Materials.begin() + id);
        if (m_SelectedIndex >= static_cast<int>(m_Materials.size())) {
            m_SelectedIndex = static_cast<int>(m_Materials.size()) - 1;
        }
        m_Dirty = true;
    }
}

std::vector<GPUMaterial> MaterialManager::GetGPUMaterials() const {
    std::vector<GPUMaterial> result;
    result.reserve(m_Materials.size());
    for (const auto& mat : m_Materials) {
        result.push_back(mat.ToGPU());
    }
    return result;
}

bool MaterialManager::SaveToFile(const std::string& path) const {
    try {
        json j = json::array();
        for (const auto& mat : m_Materials) {
            json item;
            item["name"] = mat.name;
            item["type"] = static_cast<int>(mat.type);
            item["albedo"] = { mat.albedo.r, mat.albedo.g, mat.albedo.b };
            item["roughness"] = mat.roughness;
            item["metallic"] = mat.metallic;
            item["ior"] = mat.ior;
            item["transmission"] = mat.transmission;
            item["emission"] = { mat.emission.r, mat.emission.g, mat.emission.b };
            item["emissionIntensity"] = mat.emissionIntensity;
            item["roughnessTexPath"] = mat.roughnessTexPath;
            item["normalTexPath"] = mat.normalTexPath;
            item["normalScale"] = mat.normalScale;
            item["roughnessScale"] = mat.roughnessScale;
            j.push_back(item);
        }
        std::ofstream ofs(path);
        if (!ofs.is_open()) return false;
        ofs << j.dump(2);
        LOG_I("Materials saved to {}", path);
        return true;
    } catch (const std::exception& e) {
        LOG_E("Failed to save materials: {}", e.what());
        return false;
    }
}

bool MaterialManager::LoadFromFile(const std::string& path) {
    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;
        json j;
        ifs >> j;
        m_Materials.clear();
        for (const auto& item : j) {
            Material mat;
            mat.name = item.value("name", "Unnamed");
            mat.type = static_cast<MaterialType>(item.value("type", 0));
            auto albedoArr = item.value("albedo", std::vector<float>{0.8f, 0.8f, 0.8f});
            if (albedoArr.size() >= 3) mat.albedo = glm::vec3(albedoArr[0], albedoArr[1], albedoArr[2]);
            mat.roughness = item.value("roughness", 0.5f);
            mat.metallic = item.value("metallic", 0.0f);
            mat.ior = item.value("ior", 1.5f);
            mat.transmission = item.value("transmission", 0.0f);
            auto emArr = item.value("emission", std::vector<float>{1.0f, 1.0f, 1.0f});
            if (emArr.size() >= 3) mat.emission = glm::vec3(emArr[0], emArr[1], emArr[2]);
            mat.emissionIntensity = item.value("emissionIntensity", 0.0f);
            mat.roughnessTexPath = item.value("roughnessTexPath", "");
            mat.normalTexPath = item.value("normalTexPath", "");
            mat.normalScale = item.value("normalScale", 1.0f);
            mat.roughnessScale = item.value("roughnessScale", 1.0f);
            m_Materials.push_back(mat);
        }
        m_SelectedIndex = 0;
        m_Dirty = true;
        LOG_I("Loaded {} materials from {}", m_Materials.size(), path);
        return true;
    } catch (const std::exception& e) {
        LOG_E("Failed to load materials: {}", e.what());
        return false;
    }
}

} // namespace neurender
