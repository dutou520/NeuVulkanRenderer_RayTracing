#pragma once
#include "BVH.h"
#include "MaterialManager.h"
#include <string>
#include <vector>
#include <memory>

namespace neurender {

struct SceneObject {
    std::string name;
    int materialId = 0;
    int firstTriangle = 0;
    int triangleCount = 0;
    AABB bounds;
};

class Scene {
public:
    Scene();

    bool LoadModel(const std::string& filepath);
    bool LoadOBJ(const std::string& filepath);
    bool LoadGLTF(const std::string& filepath);

    void SetCurrentModelPath(const std::string& path) { m_CurrentModelPath = path; }

    const std::vector<SceneObject>& GetObjects() const { return m_Objects; }
    std::vector<SceneObject>& GetObjects() { return m_Objects; }
    SceneObject* GetObject(int idx);
    SceneObject* FindObject(const std::string& name);

    void SetObjectMaterial(int objectIdx, int materialId);

    const BVH& GetBVH() const { return m_BVH; }
    const std::vector<TriangleData>& GetTriangles() const { return m_Triangles; }
    const std::vector<int>& GetLightTriangles() const { return m_LightTriangleIndices; }

    const AABB& GetBounds() const { return m_TotalBounds; }
    const std::string& GetCurrentModelPath() const { return m_CurrentModelPath; }

    bool IsDirty() const { return m_Dirty; }
    void SetDirty(bool dirty = true) { m_Dirty = dirty; }
    void RebuildBVH();

private:
    void AssignDefaultMaterials();
    void ExtractLightTriangles();

    std::string m_CurrentModelPath;
    std::vector<SceneObject> m_Objects;
    std::vector<TriangleData> m_Triangles;
    std::vector<int> m_LightTriangleIndices;
    AABB m_TotalBounds;
    BVH m_BVH;
    bool m_Dirty = true;
};

} // namespace neurender
