#pragma once
#include "Material.h"
#include <vector>
#include <string>
#include <memory>

namespace neurender {

class MaterialManager {
public:
    static MaterialManager& Instance();

    void Init();
    void SetupCornellBoxDefaults();

    int AddMaterial(const Material& mat);
    Material* GetMaterial(int id);
    const Material* GetMaterial(int id) const;
    int FindMaterial(const std::string& name) const;
    int GetCount() const { return static_cast<int>(m_Materials.size()); }
    const std::vector<Material>& GetAllMaterials() const { return m_Materials; }
    std::vector<Material>& GetAllMaterials() { return m_Materials; }

    void SetMaterial(int id, const Material& mat);
    void RemoveMaterial(int id);

    int GetSelectedIndex() const { return m_SelectedIndex; }
    void SetSelectedIndex(int index) { m_SelectedIndex = index; }

    bool IsDirty() const { return m_Dirty; }
    void SetDirty(bool dirty = true) { m_Dirty = dirty; }

    std::vector<GPUMaterial> GetGPUMaterials() const;

    bool SaveToFile(const std::string& path) const;
    bool LoadFromFile(const std::string& path);

private:
    MaterialManager() = default;
    std::vector<Material> m_Materials;
    int m_SelectedIndex = 0;
    bool m_Dirty = true;
};

} // namespace neurender
