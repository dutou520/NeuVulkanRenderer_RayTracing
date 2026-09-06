#pragma once
#include <glm/glm.hpp>
#include <string>

namespace neurender {

enum class MaterialType : int {
    Diffuse = 0,
    Metal = 1,
    Glass = 2,
    Emissive = 3
};

struct GPUMaterial {
    glm::vec4 albedoAndType;        // xyz = albedo, w = (float)MaterialType
    glm::vec4 emissionAndIntensity; // xyz = emission color, w = intensity
    glm::vec4 params;               // x = roughness, y = metallic, z = ior, w = transmission
    glm::ivec4 texIndices;          // x = albedoTexIdx, y = roughnessTexIdx, z = normalTexIdx, w = unused (-1 if none)
    glm::vec4 texScales;            // x = normalScale, y = roughnessScale, z = 0, w = 0
};

struct Material {
    std::string name = "Default";
    MaterialType type = MaterialType::Diffuse;
    glm::vec3 albedo = glm::vec3(0.8f);
    float roughness = 0.5f;
    float metallic = 0.0f;
    float ior = 1.5f;
    float transmission = 0.0f;
    glm::vec3 emission = glm::vec3(1.0f);
    float emissionIntensity = 0.0f;

    // Texture slots & paths
    int albedoTexIdx = -1;
    int roughnessTexIdx = -1;
    int normalTexIdx = -1;
    std::string albedoTexPath;
    std::string roughnessTexPath;
    std::string normalTexPath;

    float normalScale = 1.0f;
    float roughnessScale = 1.0f;

    GPUMaterial ToGPU() const {
        GPUMaterial gpu{};
        gpu.albedoAndType = glm::vec4(albedo, static_cast<float>(type));
        gpu.emissionAndIntensity = glm::vec4(emission, emissionIntensity);
        gpu.params = glm::vec4(roughness, metallic, ior, transmission);
        gpu.texIndices = glm::ivec4(albedoTexIdx, roughnessTexIdx, normalTexIdx, -1);
        gpu.texScales = glm::vec4(normalScale, roughnessScale, 0.0f, 0.0f);
        return gpu;
    }
};

} // namespace neurender
