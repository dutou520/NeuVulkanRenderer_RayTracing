#include "GLTFLoader.h"
#include "Scene.h"
#include "TextureManager.h"
#include "MaterialManager.h"
#include "neuLog.h"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>

namespace neurender {

// Custom file system callbacks for tinygltf to guarantee UTF-8 & Windows compatibility
static bool NeuFileExists(const std::string& abs_filename, void* user_data) {
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::u8path(abs_filename), ec);
}

static std::string NeuExpandFilePath(const std::string& filepath, void* user_data) {
    return filepath;
}

static bool NeuReadWholeFile(std::vector<unsigned char>* out, std::string* err,
                             const std::string& filepath, void* user_data) {
    std::filesystem::path p = std::filesystem::u8path(filepath);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        if (err) *err = "File not found: " + filepath;
        return false;
    }

    std::ifstream ifs(p, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        if (err) *err = "Failed to open file: " + filepath;
        return false;
    }

    std::streamsize size = ifs.tellg();
    if (size < 0) {
        if (err) *err = "Invalid file size: " + filepath;
        return false;
    }

    ifs.seekg(0, std::ios::beg);
    out->resize(static_cast<size_t>(size));
    if (!ifs.read(reinterpret_cast<char*>(out->data()), size)) {
        if (err) *err = "Failed to read file content: " + filepath;
        return false;
    }
    return true;
}

// Convert arbitrary tinygltf image buffer to 4-channel RGBA (8-bit) in memory
static std::vector<uint8_t> ConvertToRGBA(const tinygltf::Image& img) {
    std::vector<uint8_t> rgba;
    if (img.image.empty() || img.width <= 0 || img.height <= 0) return rgba;

    size_t pixelCount = static_cast<size_t>(img.width) * img.height;
    rgba.resize(pixelCount * 4);

    if (img.bits == 16 || img.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        const uint16_t* src16 = reinterpret_cast<const uint16_t*>(img.image.data());
        if (img.component == 4) {
            for (size_t i = 0; i < pixelCount; ++i) {
                rgba[i * 4 + 0] = static_cast<uint8_t>(src16[i * 4 + 0] >> 8);
                rgba[i * 4 + 1] = static_cast<uint8_t>(src16[i * 4 + 1] >> 8);
                rgba[i * 4 + 2] = static_cast<uint8_t>(src16[i * 4 + 2] >> 8);
                rgba[i * 4 + 3] = static_cast<uint8_t>(src16[i * 4 + 3] >> 8);
            }
        } else if (img.component == 3) {
            for (size_t i = 0; i < pixelCount; ++i) {
                rgba[i * 4 + 0] = static_cast<uint8_t>(src16[i * 3 + 0] >> 8);
                rgba[i * 4 + 1] = static_cast<uint8_t>(src16[i * 3 + 1] >> 8);
                rgba[i * 4 + 2] = static_cast<uint8_t>(src16[i * 3 + 2] >> 8);
                rgba[i * 4 + 3] = 255;
            }
        } else if (img.component == 2) {
            for (size_t i = 0; i < pixelCount; ++i) {
                rgba[i * 4 + 0] = static_cast<uint8_t>(src16[i * 2 + 0] >> 8);
                rgba[i * 4 + 1] = static_cast<uint8_t>(src16[i * 2 + 1] >> 8);
                rgba[i * 4 + 2] = 0;
                rgba[i * 4 + 3] = 255;
            }
        } else if (img.component == 1) {
            for (size_t i = 0; i < pixelCount; ++i) {
                uint8_t val = static_cast<uint8_t>(src16[i] >> 8);
                rgba[i * 4 + 0] = val;
                rgba[i * 4 + 1] = val;
                rgba[i * 4 + 2] = val;
                rgba[i * 4 + 3] = 255;
            }
        }
    } else {
        const uint8_t* src = img.image.data();
        if (img.component == 4) {
            std::copy(src, src + pixelCount * 4, rgba.begin());
        } else if (img.component == 3) {
            for (size_t i = 0; i < pixelCount; ++i) {
                rgba[i * 4 + 0] = src[i * 3 + 0];
                rgba[i * 4 + 1] = src[i * 3 + 1];
                rgba[i * 4 + 2] = src[i * 3 + 2];
                rgba[i * 4 + 3] = 255;
            }
        } else if (img.component == 2) {
            for (size_t i = 0; i < pixelCount; ++i) {
                rgba[i * 4 + 0] = src[i * 2 + 0];
                rgba[i * 4 + 1] = src[i * 2 + 1];
                rgba[i * 4 + 2] = 0;
                rgba[i * 4 + 3] = 255;
            }
        } else if (img.component == 1) {
            for (size_t i = 0; i < pixelCount; ++i) {
                uint8_t val = src[i];
                rgba[i * 4 + 0] = val;
                rgba[i * 4 + 1] = val;
                rgba[i * 4 + 2] = val;
                rgba[i * 4 + 3] = 255;
            }
        }
    }
    return rgba;
}

// Extract glTF metallicRoughness texture (Green = Roughness, Blue = Metallic)
// Into an in-memory RGBA image where R = Green channel (roughness) so path tracer can sample .r
static std::vector<uint8_t> ExtractRoughnessTexture(const tinygltf::Image& img) {
    std::vector<uint8_t> result;
    if (img.image.empty() || img.width <= 0 || img.height <= 0) return result;

    size_t pixelCount = static_cast<size_t>(img.width) * img.height;
    result.resize(pixelCount * 4);

    if (img.bits == 16 || img.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        const uint16_t* src16 = reinterpret_cast<const uint16_t*>(img.image.data());
        for (size_t i = 0; i < pixelCount; ++i) {
            uint8_t roughness = 255;
            uint8_t metallic = 0;
            if (img.component >= 3) {
                roughness = static_cast<uint8_t>(src16[i * img.component + 1] >> 8); // G = roughness
                metallic  = static_cast<uint8_t>(src16[i * img.component + 2] >> 8); // B = metallic
            } else if (img.component >= 2) {
                roughness = static_cast<uint8_t>(src16[i * img.component + 1] >> 8);
            } else if (img.component == 1) {
                roughness = static_cast<uint8_t>(src16[i] >> 8);
            }
            result[i * 4 + 0] = roughness;
            result[i * 4 + 1] = roughness;
            result[i * 4 + 2] = metallic;
            result[i * 4 + 3] = 255;
        }
    } else {
        const uint8_t* src = img.image.data();
        for (size_t i = 0; i < pixelCount; ++i) {
            uint8_t roughness = 255;
            uint8_t metallic = 0;
            if (img.component >= 3) {
                roughness = src[i * img.component + 1]; // G = roughness
                metallic  = src[i * img.component + 2]; // B = metallic
            } else if (img.component >= 2) {
                roughness = src[i * img.component + 1];
            } else if (img.component == 1) {
                roughness = src[i];
            }
            result[i * 4 + 0] = roughness;
            result[i * 4 + 1] = roughness;
            result[i * 4 + 2] = metallic;
            result[i * 4 + 3] = 255;
        }
    }
    return result;
}

bool LoadGLTF(const std::string& filepath, Scene& scene) {
    LOG_I("Starting GLTF/GLB import: {}", filepath);

    std::filesystem::path p = std::filesystem::u8path(filepath);
    if (!std::filesystem::exists(p)) {
        if (p.is_relative() && std::filesystem::exists("../" + filepath)) {
            p = std::filesystem::u8path("../" + filepath);
        } else {
            LOG_E("GLTF file does not exist: {}", filepath);
            return false;
        }
    }

    std::string ext = p.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
    bool isBinary = (ext == ".glb");

    tinygltf::TinyGLTF loader;
    tinygltf::FsCallbacks fsCallbacks;
    fsCallbacks.ReadWholeFile = NeuReadWholeFile;
    fsCallbacks.FileExists = NeuFileExists;
    fsCallbacks.ExpandFilePath = NeuExpandFilePath;
    fsCallbacks.WriteWholeFile = nullptr;
    fsCallbacks.user_data = nullptr;
    loader.SetFsCallbacks(fsCallbacks);

    tinygltf::Model model;
    std::string err, warn;
    bool success = false;

    if (isBinary) {
        success = loader.LoadBinaryFromFile(&model, &err, &warn, p.u8string());
    } else {
        success = loader.LoadASCIIFromFile(&model, &err, &warn, p.u8string());
    }

    if (!warn.empty()) {
        LOG_W("tinygltf warning: {}", warn);
    }
    if (!err.empty()) {
        LOG_E("tinygltf error: {}", err);
    }
    if (!success) {
        LOG_E("Failed to load glTF file: {}", filepath);
        return false;
    }

    LOG_I("glTF parsed successfully: {} meshes, {} materials, {} images, {} nodes",
          model.meshes.size(), model.materials.size(), model.images.size(), model.nodes.size());

    // -------------------------------------------------------------
    // 1. Process Textures in MEMORY (Strictly NO disk caching!)
    // -------------------------------------------------------------
    // Maps: gltfImageIndex -> vulkanTextureSlot
    std::unordered_map<int, int> gltfAlbedoSlots;
    std::unordered_map<int, int> gltfRoughnessSlots;
    std::unordered_map<int, int> gltfNormalSlots;

    auto getOrUploadAlbedo = [&](int imgIdx) -> int {
        if (imgIdx < 0 || imgIdx >= static_cast<int>(model.images.size())) return -1;
        auto it = gltfAlbedoSlots.find(imgIdx);
        if (it != gltfAlbedoSlots.end()) return it->second;

        const auto& img = model.images[imgIdx];
        std::vector<uint8_t> rgba = ConvertToRGBA(img);
        if (rgba.empty()) return -1;

        std::string texName = filepath + "#img_" + std::to_string(imgIdx) + "_albedo";
        int slot = TextureManager::Instance().CreateTextureFromMemory(texName, rgba.data(), img.width, img.height, true);
        gltfAlbedoSlots[imgIdx] = slot;
        return slot;
    };

    auto getOrUploadRoughness = [&](int imgIdx) -> int {
        if (imgIdx < 0 || imgIdx >= static_cast<int>(model.images.size())) return -1;
        auto it = gltfRoughnessSlots.find(imgIdx);
        if (it != gltfRoughnessSlots.end()) return it->second;

        const auto& img = model.images[imgIdx];
        std::vector<uint8_t> roughRGBA = ExtractRoughnessTexture(img);
        if (roughRGBA.empty()) return -1;

        std::string texName = filepath + "#img_" + std::to_string(imgIdx) + "_roughness";
        int slot = TextureManager::Instance().CreateTextureFromMemory(texName, roughRGBA.data(), img.width, img.height, false);
        gltfRoughnessSlots[imgIdx] = slot;
        return slot;
    };

    auto getOrUploadNormal = [&](int imgIdx) -> int {
        if (imgIdx < 0 || imgIdx >= static_cast<int>(model.images.size())) return -1;
        auto it = gltfNormalSlots.find(imgIdx);
        if (it != gltfNormalSlots.end()) return it->second;

        const auto& img = model.images[imgIdx];
        std::vector<uint8_t> rgba = ConvertToRGBA(img);
        if (rgba.empty()) return -1;

        std::string texName = filepath + "#img_" + std::to_string(imgIdx) + "_normal";
        int slot = TextureManager::Instance().CreateTextureFromMemory(texName, rgba.data(), img.width, img.height, false);
        gltfNormalSlots[imgIdx] = slot;
        return slot;
    };

    // -------------------------------------------------------------
    // 2. Process Materials
    // -------------------------------------------------------------
    auto& matMgr = MaterialManager::Instance();
    std::vector<int> gltfMaterialIds(model.materials.size(), 0);

    for (size_t i = 0; i < model.materials.size(); ++i) {
        const auto& gltfMat = model.materials[i];
        Material mat;
        mat.name = gltfMat.name.empty() ? ("GLTF_Mat_" + std::to_string(i)) : gltfMat.name;

        const auto& pbr = gltfMat.pbrMetallicRoughness;
        mat.albedo = glm::vec3(
            static_cast<float>(pbr.baseColorFactor[0]),
            static_cast<float>(pbr.baseColorFactor[1]),
            static_cast<float>(pbr.baseColorFactor[2])
        );
        mat.roughness = static_cast<float>(pbr.roughnessFactor);
        mat.metallic = static_cast<float>(pbr.metallicFactor);

        if (gltfMat.emissiveFactor.size() >= 3) {
            mat.emission = glm::vec3(
                static_cast<float>(gltfMat.emissiveFactor[0]),
                static_cast<float>(gltfMat.emissiveFactor[1]),
                static_cast<float>(gltfMat.emissiveFactor[2])
            );
            float emissiveLen = glm::length(mat.emission);
            mat.emissionIntensity = (emissiveLen > 0.01f) ? 5.0f : 0.0f;
        }

        // Check transmission extension (Glass)
        auto transExtIt = gltfMat.extensions.find("KHR_materials_transmission");
        if (transExtIt != gltfMat.extensions.end()) {
            if (transExtIt->second.Has("transmissionFactor")) {
                mat.transmission = static_cast<float>(transExtIt->second.Get("transmissionFactor").Get<double>());
            }
        }

        // Determine high-level material type
        if (mat.emissionIntensity > 0.01f) {
            mat.type = MaterialType::Emissive;
        } else if (mat.transmission > 0.1f) {
            mat.type = MaterialType::Glass;
        } else if (mat.metallic > 0.5f) {
            mat.type = MaterialType::Metal;
        } else {
            mat.type = MaterialType::Diffuse;
        }

        // Bind Base Color Texture
        if (pbr.baseColorTexture.index >= 0 && pbr.baseColorTexture.index < static_cast<int>(model.textures.size())) {
            int imgIdx = model.textures[pbr.baseColorTexture.index].source;
            int slot = getOrUploadAlbedo(imgIdx);
            if (slot >= 0) {
                mat.albedoTexIdx = slot;
                mat.albedoTexPath = filepath + "#img_" + std::to_string(imgIdx) + "_albedo";
            }
        }

        // Bind Metallic-Roughness Texture
        if (pbr.metallicRoughnessTexture.index >= 0 && pbr.metallicRoughnessTexture.index < static_cast<int>(model.textures.size())) {
            int imgIdx = model.textures[pbr.metallicRoughnessTexture.index].source;
            int slot = getOrUploadRoughness(imgIdx);
            if (slot >= 0) {
                mat.roughnessTexIdx = slot;
                mat.roughnessTexPath = filepath + "#img_" + std::to_string(imgIdx) + "_roughness";
            }
        }

        // Bind Normal Texture
        if (gltfMat.normalTexture.index >= 0 && gltfMat.normalTexture.index < static_cast<int>(model.textures.size())) {
            int imgIdx = model.textures[gltfMat.normalTexture.index].source;
            int slot = getOrUploadNormal(imgIdx);
            if (slot >= 0) {
                mat.normalTexIdx = slot;
                mat.normalTexPath = filepath + "#img_" + std::to_string(imgIdx) + "_normal";
                mat.normalScale = static_cast<float>(gltfMat.normalTexture.scale);
            }
        }

        int matId = matMgr.AddMaterial(mat);
        gltfMaterialIds[i] = matId;
    }

    // Default material fallback if none specified in primitive
    int defaultWhiteMatId = matMgr.FindMaterial("Cornell White");
    if (defaultWhiteMatId < 0 && matMgr.GetCount() > 0) defaultWhiteMatId = 0;

    // -------------------------------------------------------------
    // 3. Process Node Hierarchy & Primitives
    // -------------------------------------------------------------
    std::vector<TriangleData> loadedTriangles;
    std::vector<SceneObject> loadedObjects;
    AABB totalBounds;

    struct NodeStackItem {
        int nodeIdx;
        glm::mat4 transform;
    };
    std::vector<NodeStackItem> nodeStack;

    // Root nodes
    if (model.defaultScene >= 0 && model.defaultScene < static_cast<int>(model.scenes.size())) {
        for (int rootIdx : model.scenes[model.defaultScene].nodes) {
            nodeStack.push_back({ rootIdx, glm::mat4(1.0f) });
        }
    } else if (!model.scenes.empty()) {
        for (int rootIdx : model.scenes[0].nodes) {
            nodeStack.push_back({ rootIdx, glm::mat4(1.0f) });
        }
    } else {
        for (size_t i = 0; i < model.nodes.size(); ++i) {
            nodeStack.push_back({ static_cast<int>(i), glm::mat4(1.0f) });
        }
    }

    while (!nodeStack.empty()) {
        NodeStackItem item = nodeStack.back();
        nodeStack.pop_back();

        if (item.nodeIdx < 0 || item.nodeIdx >= static_cast<int>(model.nodes.size())) continue;
        const auto& node = model.nodes[item.nodeIdx];

        // Compute local matrix
        glm::mat4 localMat(1.0f);
        if (node.matrix.size() == 16) {
            localMat = glm::make_mat4(node.matrix.data());
        } else {
            glm::vec3 t(0.0f);
            if (node.translation.size() == 3) {
                t = glm::vec3(node.translation[0], node.translation[1], node.translation[2]);
            }
            glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
            if (node.rotation.size() == 4) {
                // tinygltf quaternion is [x, y, z, w]
                r = glm::quat(
                    static_cast<float>(node.rotation[3]),
                    static_cast<float>(node.rotation[0]),
                    static_cast<float>(node.rotation[1]),
                    static_cast<float>(node.rotation[2])
                );
            }
            glm::vec3 s(1.0f);
            if (node.scale.size() == 3) {
                s = glm::vec3(node.scale[0], node.scale[1], node.scale[2]);
            }
            localMat = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
        }

        glm::mat4 worldMat = item.transform * localMat;
        glm::mat3 normalMat = glm::mat3(glm::transpose(glm::inverse(worldMat)));

        // Push children
        for (int childIdx : node.children) {
            nodeStack.push_back({ childIdx, worldMat });
        }

        // Process mesh
        if (node.mesh >= 0 && node.mesh < static_cast<int>(model.meshes.size())) {
            const auto& mesh = model.meshes[node.mesh];
            std::string meshName = node.name.empty() ? (mesh.name.empty() ? ("Mesh_" + std::to_string(node.mesh)) : mesh.name) : node.name;

            for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
                const auto& prim = mesh.primitives[primIdx];

                // POSITION attribute is mandatory
                auto posIt = prim.attributes.find("POSITION");
                if (posIt == prim.attributes.end()) continue;

                const auto& posAccessor = model.accessors[posIt->second];
                const auto& posView = model.bufferViews[posAccessor.bufferView];
                const auto& posBuffer = model.buffers[posView.buffer];
                size_t vertexCount = posAccessor.count;
                if (vertexCount < 3) continue;

                size_t posStride = posAccessor.ByteStride(posView);
                const uint8_t* posBase = posBuffer.data.data() + posView.byteOffset + posAccessor.byteOffset;

                // NORMAL attribute (optional)
                const uint8_t* normBase = nullptr;
                size_t normStride = 0;
                auto normIt = prim.attributes.find("NORMAL");
                if (normIt != prim.attributes.end()) {
                    const auto& normAccessor = model.accessors[normIt->second];
                    const auto& normView = model.bufferViews[normAccessor.bufferView];
                    const auto& normBuffer = model.buffers[normView.buffer];
                    normStride = normAccessor.ByteStride(normView);
                    normBase = normBuffer.data.data() + normView.byteOffset + normAccessor.byteOffset;
                }

                // TANGENT attribute (optional)
                const uint8_t* tanBase = nullptr;
                size_t tanStride = 0;
                auto tanIt = prim.attributes.find("TANGENT");
                if (tanIt != prim.attributes.end()) {
                    const auto& tanAccessor = model.accessors[tanIt->second];
                    const auto& tanView = model.bufferViews[tanAccessor.bufferView];
                    const auto& tanBuffer = model.buffers[tanView.buffer];
                    tanStride = tanAccessor.ByteStride(tanView);
                    tanBase = tanBuffer.data.data() + tanView.byteOffset + tanAccessor.byteOffset;
                }

                // TEXCOORD_0 attribute (optional)
                const uint8_t* uvBase = nullptr;
                size_t uvStride = 0;
                int uvComponentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
                auto uvIt = prim.attributes.find("TEXCOORD_0");
                if (uvIt != prim.attributes.end()) {
                    const auto& uvAccessor = model.accessors[uvIt->second];
                    const auto& uvView = model.bufferViews[uvAccessor.bufferView];
                    const auto& uvBuffer = model.buffers[uvView.buffer];
                    uvStride = uvAccessor.ByteStride(uvView);
                    uvComponentType = uvAccessor.componentType;
                    uvBase = uvBuffer.data.data() + uvView.byteOffset + uvAccessor.byteOffset;
                }

                // Read all vertex data in world space
                std::vector<glm::vec3> positions(vertexCount);
                std::vector<glm::vec3> normals(vertexCount, glm::vec3(0.0f));
                std::vector<glm::vec4> tangents(vertexCount, glm::vec4(0.0f));
                std::vector<glm::vec2> uvs(vertexCount, glm::vec2(0.0f));
                bool hasTangents = (tanBase != nullptr);

                for (size_t v = 0; v < vertexCount; ++v) {
                    const float* pPtr = reinterpret_cast<const float*>(posBase + v * posStride);
                    glm::vec4 pWorld = worldMat * glm::vec4(pPtr[0], pPtr[1], pPtr[2], 1.0f);
                    positions[v] = glm::vec3(pWorld);

                    if (normBase) {
                        const float* nPtr = reinterpret_cast<const float*>(normBase + v * normStride);
                        normals[v] = glm::normalize(normalMat * glm::vec3(nPtr[0], nPtr[1], nPtr[2]));
                    }

                    if (tanBase) {
                        const float* tPtr = reinterpret_cast<const float*>(tanBase + v * tanStride);
                        glm::vec3 tWorld = glm::normalize(glm::mat3(worldMat) * glm::vec3(tPtr[0], tPtr[1], tPtr[2]));
                        tangents[v] = glm::vec4(tWorld, tPtr[3]);
                    }

                    if (uvBase) {
                        const uint8_t* uPtr = uvBase + v * uvStride;
                        if (uvComponentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
                            const float* fPtr = reinterpret_cast<const float*>(uPtr);
                            uvs[v] = glm::vec2(fPtr[0], fPtr[1]);
                        } else if (uvComponentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                            uvs[v] = glm::vec2(uPtr[0] / 255.0f, uPtr[1] / 255.0f);
                        } else if (uvComponentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                            const uint16_t* sPtr = reinterpret_cast<const uint16_t*>(uPtr);
                            uvs[v] = glm::vec2(sPtr[0] / 65535.0f, sPtr[1] / 65535.0f);
                        }
                    }
                }

                // Read Index Data
                std::vector<uint32_t> indices;
                if (prim.indices >= 0) {
                    const auto& idxAccessor = model.accessors[prim.indices];
                    const auto& idxView = model.bufferViews[idxAccessor.bufferView];
                    const auto& idxBuffer = model.buffers[idxView.buffer];
                    size_t idxCount = idxAccessor.count;
                    indices.resize(idxCount);

                    const uint8_t* idxBase = idxBuffer.data.data() + idxView.byteOffset + idxAccessor.byteOffset;
                    if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        const uint16_t* ptr = reinterpret_cast<const uint16_t*>(idxBase);
                        for (size_t k = 0; k < idxCount; ++k) indices[k] = ptr[k];
                    } else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        const uint32_t* ptr = reinterpret_cast<const uint32_t*>(idxBase);
                        for (size_t k = 0; k < idxCount; ++k) indices[k] = ptr[k];
                    } else if (idxAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        for (size_t k = 0; k < idxCount; ++k) indices[k] = idxBase[k];
                    }
                } else {
                    indices.resize(vertexCount);
                    for (size_t k = 0; k < vertexCount; ++k) indices[k] = static_cast<uint32_t>(k);
                }

                // Determine Material
                int matId = defaultWhiteMatId;
                if (prim.material >= 0 && prim.material < static_cast<int>(gltfMaterialIds.size())) {
                    matId = gltfMaterialIds[prim.material];
                }

                // Create Scene Object
                SceneObject obj;
                obj.name = mesh.primitives.size() > 1 ? (meshName + "_" + std::to_string(primIdx)) : meshName;
                obj.materialId = matId;
                obj.firstTriangle = static_cast<int>(loadedTriangles.size());

                int curObjId = static_cast<int>(loadedObjects.size());

                // Assemble Triangles according to primitive mode
                auto addTriangle = [&](uint32_t i0, uint32_t i1, uint32_t i2) {
                    if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) return;

                    TriangleData tri;
                    tri.objectId = curObjId;
                    tri.materialId = matId;
                    tri.v0 = positions[i0];
                    tri.v1 = positions[i1];
                    tri.v2 = positions[i2];
                    tri.n0 = normals[i0];
                    tri.n1 = normals[i1];
                    tri.n2 = normals[i2];
                    tri.uv0 = uvs[i0];
                    tri.uv1 = uvs[i1];
                    tri.uv2 = uvs[i2];

                    // Geometric normal fallback
                    glm::vec3 geoNormal = glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0);
                    float len = glm::length(geoNormal);
                    if (len > 1e-6f) geoNormal /= len;
                    else geoNormal = glm::vec3(0.0f, 1.0f, 0.0f);

                    if (glm::length(tri.n0) < 0.1f) tri.n0 = geoNormal;
                    if (glm::length(tri.n1) < 0.1f) tri.n1 = geoNormal;
                    if (glm::length(tri.n2) < 0.1f) tri.n2 = geoNormal;

                    // Tangent and bitangent
                    if (hasTangents) {
                        glm::vec3 avgT = glm::vec3(tangents[i0]) + glm::vec3(tangents[i1]) + glm::vec3(tangents[i2]);
                        float tLen = glm::length(avgT);
                        if (tLen > 1e-4f) avgT /= tLen;
                        else avgT = (std::abs(geoNormal.x) > 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                        tri.tangent = glm::vec4(avgT, tangents[i0].w);
                    } else {
                        // Compute tangent and bitangent geometrically
                        glm::vec3 edge1 = tri.v1 - tri.v0;
                        glm::vec3 edge2 = tri.v2 - tri.v0;
                        glm::vec2 deltaUV1 = tri.uv1 - tri.uv0;
                        glm::vec2 deltaUV2 = tri.uv2 - tri.uv0;
                        float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;

                        glm::vec3 tangent(1.0f, 0.0f, 0.0f);
                        float bitangentSign = 1.0f;

                        if (std::abs(det) > 1e-6f) {
                            float invDet = 1.0f / det;
                            tangent = invDet * (edge1 * deltaUV2.y - edge2 * deltaUV1.y);
                            glm::vec3 bitangent = invDet * (edge2 * deltaUV1.x - edge1 * deltaUV2.x);

                            tangent = glm::normalize(tangent - geoNormal * glm::dot(geoNormal, tangent));
                            if (glm::length(tangent) < 0.1f) {
                                tangent = (std::abs(geoNormal.x) > 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                                tangent = glm::normalize(tangent - geoNormal * glm::dot(geoNormal, tangent));
                            }
                            bitangentSign = (glm::dot(glm::cross(geoNormal, tangent), bitangent) < 0.0f) ? -1.0f : 1.0f;
                        } else {
                            tangent = (std::abs(geoNormal.x) > 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                            tangent = glm::normalize(tangent - geoNormal * glm::dot(geoNormal, tangent));
                            bitangentSign = 1.0f;
                        }
                        tri.tangent = glm::vec4(tangent, bitangentSign);
                    }

                    obj.bounds.Grow(tri.v0);
                    obj.bounds.Grow(tri.v1);
                    obj.bounds.Grow(tri.v2);
                    totalBounds.Grow(tri.v0);
                    totalBounds.Grow(tri.v1);
                    totalBounds.Grow(tri.v2);

                    loadedTriangles.push_back(tri);
                };

                int mode = (prim.mode < 0) ? TINYGLTF_MODE_TRIANGLES : prim.mode;
                if (mode == TINYGLTF_MODE_TRIANGLES) {
                    for (size_t k = 0; k + 2 < indices.size(); k += 3) {
                        addTriangle(indices[k], indices[k + 1], indices[k + 2]);
                    }
                } else if (mode == TINYGLTF_MODE_TRIANGLE_STRIP) {
                    for (size_t k = 0; k + 2 < indices.size(); ++k) {
                        if (k % 2 == 0) addTriangle(indices[k], indices[k + 1], indices[k + 2]);
                        else addTriangle(indices[k], indices[k + 2], indices[k + 1]);
                    }
                } else if (mode == TINYGLTF_MODE_TRIANGLE_FAN) {
                    for (size_t k = 1; k + 1 < indices.size(); ++k) {
                        addTriangle(indices[0], indices[k], indices[k + 1]);
                    }
                }

                obj.triangleCount = static_cast<int>(loadedTriangles.size()) - obj.firstTriangle;
                if (obj.triangleCount > 0) {
                    loadedObjects.push_back(obj);
                }
            }
        }
    }

    if (loadedTriangles.empty()) {
        LOG_E("GLTF file contains no valid triangles: {}", filepath);
        return false;
    }

    // -------------------------------------------------------------
    // 4. Update Scene State
    // -------------------------------------------------------------
    scene.GetObjects() = std::move(loadedObjects);
    auto& sceneTriangles = const_cast<std::vector<TriangleData>&>(scene.GetTriangles());
    sceneTriangles = std::move(loadedTriangles);

    // Update scene total bounds
    scene.RebuildBVH();
    scene.SetDirty(true);

    LOG_I("Successfully loaded GLTF scene '{}': {} objects, {} triangles.",
          filepath, scene.GetObjects().size(), scene.GetTriangles().size());
    return true;
}

} // namespace neurender
