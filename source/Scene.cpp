#include "Scene.h"
#include "neuLog.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include <filesystem>
#include <fstream>
#include <algorithm>

namespace neurender {

Scene::Scene() {
}

bool Scene::LoadOBJ(const std::string& filepath) {
    LOG_I("Loading OBJ file: {}", filepath);

    std::filesystem::path p = std::filesystem::u8path(filepath);
    if (!std::filesystem::exists(p)) {
        LOG_E("File does not exist: {}", filepath);
        return false;
    }

    std::ifstream ifs(p);
    if (!ifs.is_open()) {
        LOG_E("Failed to open file stream: {}", filepath);
        return false;
    }

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, &ifs, nullptr, true);
    if (!err.empty()) {
        LOG_W("TinyObjLoader message: {}", err);
    }
    if (!ret) {
        LOG_E("Failed to parse OBJ file: {}", filepath);
        return false;
    }

    LOG_I("Parsed OBJ: {} shapes, {} vertices, {} normals",
          shapes.size(), attrib.vertices.size() / 3, attrib.normals.size() / 3);

    m_Objects.clear();
    m_Triangles.clear();
    m_TotalBounds = AABB{};

    auto& matMgr = MaterialManager::Instance();

    for (size_t s = 0; s < shapes.size(); ++s) {
        const auto& shape = shapes[s];
        SceneObject obj;
        obj.name = shape.name.empty() ? ("Object_" + std::to_string(s)) : shape.name;
        obj.firstTriangle = static_cast<int>(m_Triangles.size());
        obj.materialId = 0; // Will be assigned later

        size_t indexOffset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
            size_t fv = shape.mesh.num_face_vertices[f];
            if (fv != 3) {
                indexOffset += fv;
                continue;
            }

            TriangleData tri;
            tri.objectId = static_cast<int>(m_Objects.size());

            for (size_t v = 0; v < 3; ++v) {
                tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

                glm::vec3 pos(
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]
                );

                glm::vec3 norm(0.0f);
                if (idx.normal_index >= 0) {
                    norm = glm::vec3(
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]
                    );
                }

                glm::vec2 uv(0.0f);
                if (idx.texcoord_index >= 0) {
                    uv = glm::vec2(
                        attrib.texcoords[2 * idx.texcoord_index + 0],
                        attrib.texcoords[2 * idx.texcoord_index + 1]
                    );
                }

                if (v == 0) { tri.v0 = pos; tri.n0 = norm; tri.uv0 = uv; }
                else if (v == 1) { tri.v1 = pos; tri.n1 = norm; tri.uv1 = uv; }
                else { tri.v2 = pos; tri.n2 = norm; tri.uv2 = uv; }

                obj.bounds.Grow(pos);
                m_TotalBounds.Grow(pos);
            }

            // Compute geometric normal if normals are missing or zero
            glm::vec3 geoNormal = glm::cross(tri.v1 - tri.v0, tri.v2 - tri.v0);
            float len = glm::length(geoNormal);
            if (len > 1e-6f) geoNormal /= len;
            else geoNormal = glm::vec3(0.0f, 1.0f, 0.0f);

            if (glm::length(tri.n0) < 0.1f) tri.n0 = geoNormal;
            if (glm::length(tri.n1) < 0.1f) tri.n1 = geoNormal;
            if (glm::length(tri.n2) < 0.1f) tri.n2 = geoNormal;

            // Compute tangent space (TBN)
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

            m_Triangles.push_back(tri);
            indexOffset += 3;
        }

        obj.triangleCount = static_cast<int>(m_Triangles.size()) - obj.firstTriangle;
        if (obj.triangleCount > 0) {
            m_Objects.push_back(obj);
        }
    }

    m_CurrentModelPath = filepath;
    AssignDefaultMaterials();
    RebuildBVH();

    LOG_I("Loaded scene with {} objects and {} triangles.", m_Objects.size(), m_Triangles.size());
    return true;
}

void Scene::AssignDefaultMaterials() {
    auto& matMgr = MaterialManager::Instance();

    for (size_t i = 0; i < m_Objects.size(); ++i) {
        auto& obj = m_Objects[i];
        std::string n = obj.name;
        std::string lower = n;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });

        int matId = 0; // Default White
        if (lower.find("wall_left") != std::string::npos) {
            matId = matMgr.FindMaterial("Cornell Red");
        } else if (lower.find("wall_right") != std::string::npos) {
            matId = matMgr.FindMaterial("Cornell Green");
        } else if (lower.find("emmision") != std::string::npos || lower.find("light") != std::string::npos) {
            matId = matMgr.FindMaterial("Ceiling Light");
        } else if (n.find("猴头") != std::string::npos || lower.find("monkey") != std::string::npos || lower.find("suzanne") != std::string::npos) {
            matId = matMgr.FindMaterial("Gold (Monkey)");
        } else if (n.find("球") != std::string::npos || lower.find("sphere") != std::string::npos) {
            matId = matMgr.FindMaterial("Glass (Sphere)");
        } else if (lower.find("box") != std::string::npos) {
            matId = matMgr.FindMaterial("Box Material");
        } else if (n.find("锥") != std::string::npos || lower.find("cone") != std::string::npos) {
            matId = matMgr.FindMaterial("Blue Metal (Cone)");
        } else {
            matId = matMgr.FindMaterial("Cornell White");
        }

        if (matId < 0) matId = 0;
        obj.materialId = matId;

        for (int t = obj.firstTriangle; t < obj.firstTriangle + obj.triangleCount; ++t) {
            m_Triangles[t].materialId = matId;
            m_Triangles[t].objectId = static_cast<int>(i);
        }

        LOG_I("Assigned material '{}' (id={}) to object '{}'",
              matMgr.GetMaterial(matId) ? matMgr.GetMaterial(matId)->name : "unknown",
              matId, obj.name);
    }
}

void Scene::SetObjectMaterial(int objectIdx, int materialId) {
    if (objectIdx >= 0 && objectIdx < static_cast<int>(m_Objects.size())) {
        m_Objects[objectIdx].materialId = materialId;
        for (int t = m_Objects[objectIdx].firstTriangle;
             t < m_Objects[objectIdx].firstTriangle + m_Objects[objectIdx].triangleCount; ++t) {
            m_Triangles[t].materialId = materialId;
        }
        RebuildBVH();
    }
}

void Scene::ExtractLightTriangles() {
    m_LightTriangleIndices.clear();
    auto& matMgr = MaterialManager::Instance();

    for (size_t i = 0; i < m_Triangles.size(); ++i) {
        int mId = m_Triangles[i].materialId;
        const Material* mat = matMgr.GetMaterial(mId);
        if (mat && (mat->type == MaterialType::Emissive || mat->emissionIntensity > 0.01f)) {
            m_LightTriangleIndices.push_back(static_cast<int>(i));
        }
    }
    LOG_I("Extracted {} emissive triangles for NEE direct light sampling.", m_LightTriangleIndices.size());
}

void Scene::RebuildBVH() {
    m_BVH.Build(m_Triangles);
    ExtractLightTriangles();
    m_Dirty = true;
}

SceneObject* Scene::GetObject(int idx) {
    if (idx >= 0 && idx < static_cast<int>(m_Objects.size())) {
        return &m_Objects[idx];
    }
    return nullptr;
}

SceneObject* Scene::FindObject(const std::string& name) {
    for (auto& obj : m_Objects) {
        if (obj.name == name) {
            return &obj;
        }
    }
    return nullptr;
}

} // namespace neurender
