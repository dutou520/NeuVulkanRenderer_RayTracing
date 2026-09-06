#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <functional>

namespace neurender {

class FileBrowser {
public:
    FileBrowser();

    void Render();
    void SetCurrentPath(const std::string& path);
    const std::string& GetCurrentPath() const { return m_CurrentPath; }

    const std::string& GetSelectedFile() const { return m_SelectedFile; }
    void SetSelectedFile(const std::string& file) { m_SelectedFile = file; }

    void SetFileSelectedCallback(std::function<void(const std::string&)> cb) {
        m_OnSelectedCallback = cb;
    }

private:
    void Refresh();

    std::string m_CurrentPath;
    std::string m_SelectedFile;
    std::vector<std::filesystem::directory_entry> m_Entries;
    std::function<void(const std::string&)> m_OnSelectedCallback;

    int m_FilterIndex = 0; // 0 = .obj, 1 = .json, 2 = all
};

} // namespace neurender
