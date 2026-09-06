#include "FileBrowser.h"
#include <imgui.h>
#include "neuLog.h"
#include <algorithm>

namespace neurender {

FileBrowser::FileBrowser() {
    std::string defaultPath = "resource/models";
    if (std::filesystem::exists(std::filesystem::u8path(defaultPath))) {
        SetCurrentPath(defaultPath);
    } else {
        SetCurrentPath(std::filesystem::current_path().u8string());
    }
}

void FileBrowser::SetCurrentPath(const std::string& path) {
    try {
        std::filesystem::path p = std::filesystem::u8path(path);
        if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
            m_CurrentPath = std::filesystem::canonical(p).u8string();
            std::replace(m_CurrentPath.begin(), m_CurrentPath.end(), '\\', '/');
            Refresh();
        }
    } catch (const std::exception& e) {
        LOG_W("FileBrowser failed to set path {}: {}", path, e.what());
    }
}

void FileBrowser::Refresh() {
    m_Entries.clear();
    try {
        std::filesystem::path p = std::filesystem::u8path(m_CurrentPath);
        for (const auto& entry : std::filesystem::directory_iterator(p)) {
            m_Entries.push_back(entry);
        }
        // Sort: directories first, then alphabetical
        std::sort(m_Entries.begin(), m_Entries.end(), [](const auto& a, const auto& b) {
            bool aDir = a.is_directory();
            bool bDir = b.is_directory();
            if (aDir != bDir) return aDir > bDir;
            return a.path().filename().string() < b.path().filename().string();
        });
    } catch (const std::exception& e) {
        LOG_W("Error iterating directory {}: {}", m_CurrentPath, e.what());
    }
}

void FileBrowser::Render() {
    ImGui::Text("当前路径: %s", m_CurrentPath.c_str());

    // Navigation and bookmarks
    if (ImGui::Button("上级目录 (..)")) {
        std::filesystem::path parent = std::filesystem::u8path(m_CurrentPath).parent_path();
        if (!parent.empty()) {
            SetCurrentPath(parent.u8string());
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("刷新")) {
        Refresh();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| 快捷书签:");
    ImGui::SameLine();
    if (ImGui::Button("模型目录")) {
        SetCurrentPath("resource/models");
    }
    ImGui::SameLine();
    if (ImGui::Button("材质贴图目录")) {
        SetCurrentPath("resource/textures");
    }
    ImGui::SameLine();
    if (ImGui::Button("工程根目录")) {
        SetCurrentPath(std::filesystem::current_path().u8string());
    }
    ImGui::SameLine();
    if (ImGui::Button("C 盘")) {
        SetCurrentPath("C:/");
    }
    ImGui::SameLine();
    if (ImGui::Button("D 盘")) {
        SetCurrentPath("D:/");
    }

    ImGui::SameLine();
    const char* filters[] = { "OBJ 模型 (*.obj)", "材质 JSON (*.json)", "纹理贴图 (*.png;*.jpg...)", "所有文件 (*.*)" };
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("##Filter", &m_FilterIndex, filters, 4)) {
        // filter changed
    }

    ImGui::Separator();

    // File list table
    if (ImGui::BeginTable("FileTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 260))) {
        ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("大小", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();

        for (const auto& entry : m_Entries) {
            bool isDir = entry.is_directory();
            std::string filename = entry.path().filename().u8string();
            std::string ext = entry.path().extension().u8string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

            // Apply filter
            if (!isDir) {
                if (m_FilterIndex == 0 && ext != ".obj") continue;
                if (m_FilterIndex == 1 && ext != ".json") continue;
                if (m_FilterIndex == 2 && ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".tga" && ext != ".bmp") continue;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            std::string label = (isDir ? "[文件夹] " : "[文件] ") + filename;
            bool isSelected = (!isDir && m_SelectedFile == entry.path().u8string());

            if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                if (isDir) {
                    SetCurrentPath(entry.path().u8string());
                    break;
                } else {
                    m_SelectedFile = entry.path().u8string();
                    std::replace(m_SelectedFile.begin(), m_SelectedFile.end(), '\\', '/');
                }
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                if (!isDir && m_OnSelectedCallback) {
                    m_OnSelectedCallback(m_SelectedFile);
                }
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(isDir ? "Folder" : ext.c_str());

            ImGui::TableSetColumnIndex(2);
            if (!isDir) {
                try {
                    auto size = entry.file_size();
                    if (size < 1024) ImGui::Text("%llu B", size);
                    else if (size < 1024 * 1024) ImGui::Text("%.1f KB", size / 1024.0f);
                    else ImGui::Text("%.2f MB", size / (1024.0f * 1024.0f));
                } catch (...) {
                    ImGui::TextUnformatted("-");
                }
            } else {
                ImGui::TextUnformatted("-");
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (!m_SelectedFile.empty()) {
        ImGui::Text("选中文件: %s", m_SelectedFile.c_str());
        ImGui::SameLine();
        if (ImGui::Button("加载选中模型", ImVec2(120, 0))) {
            if (m_OnSelectedCallback) {
                m_OnSelectedCallback(m_SelectedFile);
            }
        }
    } else {
        ImGui::TextDisabled("未选择文件（单击选择，双击或点击按钮加载）");
    }
}

} // namespace neurender
