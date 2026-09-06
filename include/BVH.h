#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace neurender {

struct GPUTriangle {
    glm::vec4 v0;      // xyz = v0, w = materialId
    glm::vec4 v1;      // xyz = v1, w = objectId
    glm::vec4 v2;      // xyz = v2, w = 0.0f
    glm::vec4 n0;      // xyz = n0, w = uv0.x
    glm::vec4 n1;      // xyz = n1, w = uv0.y
    glm::vec4 n2;      // xyz = n2, w = uv1.x
    glm::vec4 uv12;    // x = uv1.y, y = uv2.x, z = uv2.y, w = 0.0f
    glm::vec4 tangent; // xyz = tangent, w = bitangentSign (+1.0f or -1.0f)
};

struct GPUBVHNode {
    glm::vec4 aabbMinAndLeft;  // xyz = min, w = leftChild / primStart
    glm::vec4 aabbMaxAndCount; // xyz = max, w = primCount (if > 0, leaf!)
};

struct TriangleData {
    glm::vec3 v0, v1, v2;
    glm::vec3 n0, n1, n2;
    glm::vec2 uv0{ 0.0f }, uv1{ 0.0f }, uv2{ 0.0f };
    glm::vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
    int materialId = 0;
    int objectId = 0;

    glm::vec3 Centroid() const {
        return (v0 + v1 + v2) * (1.0f / 3.0f);
    }
};

struct AABB {
    glm::vec3 min = glm::vec3(1e30f);
    glm::vec3 max = glm::vec3(-1e30f);

    void Grow(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }

    void Grow(const AABB& b) {
        min = glm::min(min, b.min);
        max = glm::max(max, b.max);
    }

    float Area() const {
        glm::vec3 e = max - min;
        return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
    }
};

class BVH {
public:
    void Build(const std::vector<TriangleData>& triangles);

    const std::vector<GPUBVHNode>& GetNodes() const { return m_FlatNodes; }
    const std::vector<GPUTriangle>& GetTriangles() const { return m_GPUTriangles; }
    const std::vector<int>& GetEmissiveTriangles() const { return m_EmissiveIndices; }

private:
    struct BuildNode {
        AABB box;
        int left = -1;
        int right = -1;
        int primStart = 0;
        int primCount = 0;
    };

    int Subdivide(int nodeIdx, int depth);

    std::vector<TriangleData> m_Triangles;
    std::vector<BuildNode> m_BuildNodes;
    std::vector<GPUBVHNode> m_FlatNodes;
    std::vector<GPUTriangle> m_GPUTriangles;
    std::vector<int> m_EmissiveIndices;

    void Flatten(int nodeIdx);
};

} // namespace neurender
