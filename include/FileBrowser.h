#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include "TextureManager.h"

namespace neurender {

class FileBrowser {
public:
    FileBrowser();
    ~FileBrowser();

    void Render();
    void Shutdown();

    void SetCurrentPath(const std::string& path);
    const std::string& GetCurrentPath() const { return m_CurrentPath; }

    const std::string& GetSelectedFile() const { return m_SelectedFile; }
    void SetSelectedFile(const std::string& file);

    void SetFileSelectedCallback(std::function<void(const std::string&)> cb) {
        m_OnSelectedCallback = cb;
    }

private:
    void Refresh();
    void UpdatePreview(const std::string& filepath);
    void ClearPreview();

    void RenderFileListPanel(float width);
    void RenderPreviewPanel();

    std::string m_CurrentPath;
    std::string m_SelectedFile;
    std::vector<std::filesystem::directory_entry> m_Entries;
    std::function<void(const std::string&)> m_OnSelectedCallback;

    int m_FilterIndex = 0; // 0 = .obj, 1 = .json, 2 = image (*.png;*.jpg...), 3 = all

    // Preview state
    UIPreviewTexture m_PreviewTexture{};
    std::string m_LastPreviewPath;
    float m_SplitRatio = 0.45f;
    bool m_FitMode = true;
    float m_ZoomScale = 1.0f;
    bool m_ShowCheckerboard = true;
};

} // namespace neurender
