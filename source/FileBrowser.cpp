#include "FileBrowser.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "neuLog.h"
#include "MaterialManager.h"
#include "PathTracerCore.h"
#include <algorithm>
#include <cmath>

namespace neurender {

static bool IsImageExtension(const std::string& ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".bmp" || ext == ".tga" || ext == ".hdr" || ext == ".webp";
}

static bool IsModelExtension(const std::string& ext) {
    return ext == ".obj" || ext == ".gltf" || ext == ".glb";
}

FileBrowser::FileBrowser() {
    std::string defaultPath = "resource/models";
    if (std::filesystem::exists(std::filesystem::u8path(defaultPath))) {
        SetCurrentPath(defaultPath);
    } else {
        SetCurrentPath(std::filesystem::current_path().u8string());
    }
}

FileBrowser::~FileBrowser() {
    ClearPreview();
}

void FileBrowser::Shutdown() {
    ClearPreview();
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

void FileBrowser::SetSelectedFile(const std::string& file) {
    m_SelectedFile = file;
    std::replace(m_SelectedFile.begin(), m_SelectedFile.end(), '\\', '/');
    UpdatePreview(m_SelectedFile);
}

void FileBrowser::ClearPreview() {
    if (m_PreviewTexture.IsValid()) {
        TextureManager::Instance().DestroyUIPreviewTexture(m_PreviewTexture);
    }
    m_LastPreviewPath.clear();
}

void FileBrowser::UpdatePreview(const std::string& filepath) {
    if (filepath == m_LastPreviewPath) return;

    ClearPreview();
    m_LastPreviewPath = filepath;

    if (filepath.empty()) return;

    std::filesystem::path p = std::filesystem::u8path(filepath);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || std::filesystem::is_directory(p, ec)) {
        return;
    }

    std::string ext = p.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

    if (IsImageExtension(ext)) {
        m_PreviewTexture = TextureManager::Instance().CreateUIPreviewTexture(filepath);
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
    // Top bar: Current path display
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
    const char* filters[] = { "3D 模型 (*.obj;*.gltf;*.glb)", "材质 JSON (*.json)", "纹理贴图 (*.png;*.jpg...)", "所有文件 (*.*)" };
    ImGui::SetNextItemWidth(185);
    if (ImGui::Combo("##Filter", &m_FilterIndex, filters, 4)) {
        // filter changed
    }

    ImGui::Separator();

    // Main content: Horizontal split layout (Left: File List, Right: Image/Resource Preview)
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 100.0f || avail.y < 80.0f) {
        return;
    }

    const float splitterThickness = 6.0f;
    float totalW = avail.x - splitterThickness;
    float leftW = std::floor(totalW * m_SplitRatio);
    if (leftW < 220.0f) leftW = 220.0f;
    if (leftW > totalW - 180.0f) leftW = totalW - 180.0f;

    // --- Left Panel: File List ---
    ImGui::BeginChild("FileListPane", ImVec2(leftW, avail.y), true);
    RenderFileListPanel(leftW);
    ImGui::EndChild();

    ImGui::SameLine(0, 0);

    // --- Middle Splitter (Draggable) ---
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.59f, 0.98f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.59f, 0.98f, 0.45f));
    ImGui::Button("##Splitter", ImVec2(splitterThickness, avail.y));
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
        float delta = ImGui::GetIO().MouseDelta.x;
        if (totalW > 50.0f) {
            m_SplitRatio += delta / totalW;
            m_SplitRatio = std::clamp(m_SplitRatio, 0.20f, 0.80f);
        }
    }

    ImGui::SameLine(0, 0);

    // --- Right Panel: Image / Resource Preview Window ---
    ImGui::BeginChild("ImagePreviewPane", ImVec2(0, avail.y), true);
    RenderPreviewPanel();
    ImGui::EndChild();
}

void FileBrowser::RenderFileListPanel(float width) {
    float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2.2f;
    float tableHeight = ImGui::GetContentRegionAvail().y - footerHeight;
    if (tableHeight < 100.0f) tableHeight = 100.0f;

    // File list table
    if (ImGui::BeginTable("FileTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, tableHeight))) {
        ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("类型", ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn("大小", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        for (const auto& entry : m_Entries) {
            bool isDir = entry.is_directory();
            std::string filename = entry.path().filename().u8string();
            std::string ext = entry.path().extension().u8string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

            // Apply filter
            if (!isDir) {
                if (m_FilterIndex == 0 && !IsModelExtension(ext)) continue;
                if (m_FilterIndex == 1 && ext != ".json") continue;
                if (m_FilterIndex == 2 && !IsImageExtension(ext)) continue;
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
                    UpdatePreview(m_SelectedFile);
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
    ImGui::Separator();

    // Footer actions
    if (!m_SelectedFile.empty()) {
        std::filesystem::path selPath = std::filesystem::u8path(m_SelectedFile);
        std::string filename = selPath.filename().u8string();
        std::string ext = selPath.extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

        ImGui::Text("已选: %s", filename.c_str());
        ImGui::SameLine();
        if (IsModelExtension(ext)) {
            if (ImGui::Button("加载选中模型", ImVec2(120, 0))) {
                if (m_OnSelectedCallback) {
                    m_OnSelectedCallback(m_SelectedFile);
                }
            }
        } else if (ext == ".json") {
            if (ImGui::Button("导入材质预设", ImVec2(120, 0))) {
                if (MaterialManager::Instance().LoadFromFile(m_SelectedFile)) {
                    PathTracerCore::ResetAccumulation();
                    LOG_I("Imported material from {}", m_SelectedFile);
                }
            }
        } else if (IsImageExtension(ext)) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "(在右侧窗口预览中)");
        }
    } else {
        ImGui::TextDisabled("单击文件可在右侧预览图片；双击加载模型");
    }
}

void FileBrowser::RenderPreviewPanel() {
    if (m_PreviewTexture.IsValid()) {
        std::filesystem::path p = std::filesystem::u8path(m_PreviewTexture.path);
        std::string filename = p.filename().u8string();

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float infoWidth = 195.0f;
        if (avail.x < 380.0f) {
            infoWidth = avail.x * 0.46f;
        }

        // --- Left: Info & Control Pane ---
        ImGui::BeginChild("PreviewInfoPane", ImVec2(infoWidth, avail.y), false);

        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "[图片预览] (Preview)");
        ImGui::Separator();

        // Filename
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", filename.c_str());

        // Resolution & Specs
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%d × %d", m_PreviewTexture.width, m_PreviewTexture.height);
        try {
            auto size = std::filesystem::file_size(p);
            char sizeBuf[32];
            if (size < 1024) snprintf(sizeBuf, sizeof(sizeBuf), "%llu B", size);
            else if (size < 1024 * 1024) snprintf(sizeBuf, sizeof(sizeBuf), "%.1f KB", size / 1024.0f);
            else snprintf(sizeBuf, sizeof(sizeBuf), "%.2f MB", size / (1024.0f * 1024.0f));
            ImGui::Text("%s | %s", m_PreviewTexture.channels == 4 ? "RGBA" : "RGB", sizeBuf);
        } catch (...) {
            ImGui::Text("%s", m_PreviewTexture.channels == 4 ? "RGBA" : "RGB");
        }

        ImGui::Spacing();
        ImGui::Checkbox("适应窗口 (Fit)", &m_FitMode);
        ImGui::Checkbox("透明棋盘格", &m_ShowCheckerboard);

        if (!m_FitMode) {
            ImGui::SetNextItemWidth(infoWidth - 55.0f);
            ImGui::SliderFloat("##Zoom", &m_ZoomScale, 0.1f, 4.0f, "%.1fx");
            ImGui::SameLine();
            if (ImGui::SmallButton("1:1")) {
                m_ZoomScale = 1.0f;
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        // Material quick assign
        auto& matMgr = MaterialManager::Instance();
        int selMatIdx = matMgr.GetSelectedIndex();
        Material* curMat = matMgr.GetMaterial(selMatIdx);
        if (!curMat && matMgr.GetCount() > 0) {
            curMat = matMgr.GetMaterial(0);
        }
        if (curMat) {
            ImGui::TextDisabled("材质赋予 (当前: %s):", curMat->name.c_str());
            if (ImGui::Button("设为漫反射", ImVec2(-1, 0))) {
                curMat->albedoTexPath = m_PreviewTexture.path;
                curMat->albedoTexIdx = TextureManager::Instance().LoadTexture(curMat->albedoTexPath, true);
                matMgr.SetDirty(true);
                PathTracerCore::ResetAccumulation();
                LOG_I("Set material '{}' Albedo to '{}'", curMat->name, m_PreviewTexture.path);
            }
            if (ImGui::Button("设为粗糙度", ImVec2(-1, 0))) {
                curMat->roughnessTexPath = m_PreviewTexture.path;
                curMat->roughnessTexIdx = TextureManager::Instance().LoadTexture(curMat->roughnessTexPath, false);
                matMgr.SetDirty(true);
                PathTracerCore::ResetAccumulation();
                LOG_I("Set material '{}' Roughness to '{}'", curMat->name, m_PreviewTexture.path);
            }
            if (ImGui::Button("设为法线贴图", ImVec2(-1, 0))) {
                curMat->normalTexPath = m_PreviewTexture.path;
                curMat->normalTexIdx = TextureManager::Instance().LoadTexture(curMat->normalTexPath, false);
                matMgr.SetDirty(true);
                PathTracerCore::ResetAccumulation();
                LOG_I("Set material '{}' Normal to '{}'", curMat->name, m_PreviewTexture.path);
            }
        }

        ImGui::EndChild();

        // --- Right: Image Display Viewport (Picture on the right) ---
        ImGui::SameLine();
        ImGui::BeginChild("PreviewViewport", ImVec2(0, avail.y), true, ImGuiWindowFlags_HorizontalScrollbar);

        ImVec2 availVP = ImGui::GetContentRegionAvail();
        float texW = static_cast<float>(m_PreviewTexture.width);
        float texH = static_cast<float>(m_PreviewTexture.height);
        ImVec2 displaySize;

        if (m_FitMode) {
            float aspect = texW / texH;
            float dispW = availVP.x;
            float dispH = dispW / aspect;
            if (dispH > availVP.y && availVP.y > 20.0f) {
                dispH = availVP.y;
                dispW = dispH * aspect;
            }
            if (dispW < 1.0f) dispW = 1.0f;
            if (dispH < 1.0f) dispH = 1.0f;
            displaySize = ImVec2(dispW, dispH);

            // Center image
            float offsetX = (availVP.x - dispW) * 0.5f;
            float offsetY = (availVP.y - dispH) * 0.5f;
            if (offsetX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
            if (offsetY > 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
        } else {
            displaySize = ImVec2(texW * m_ZoomScale, texH * m_ZoomScale);
        }

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + displaySize.x, p0.y + displaySize.y);

        // Draw alpha checkerboard background
        if (m_ShowCheckerboard) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float tileSize = 16.0f;
            ImU32 col1 = IM_COL32(42, 42, 42, 255);
            ImU32 col2 = IM_COL32(65, 65, 65, 255);
            for (float y = p0.y; y < p1.y; y += tileSize) {
                for (float x = p0.x; x < p1.x; x += tileSize) {
                    int tx = static_cast<int>((x - p0.x) / tileSize);
                    int ty = static_cast<int>((y - p0.y) / tileSize);
                    ImU32 c = ((tx + ty) % 2 == 0) ? col1 : col2;
                    ImVec2 tp0(x, y);
                    ImVec2 tp1(std::min(x + tileSize, p1.x), std::min(y + tileSize, p1.y));
                    drawList->AddRectFilled(tp0, tp1, c);
                }
            }
        }

        // Draw texture image
        ImGui::Image(reinterpret_cast<ImTextureID>(m_PreviewTexture.descriptorSet), displaySize);

        ImGui::EndChild();

    } else if (!m_SelectedFile.empty()) {
        // Selected a non-image file
        std::filesystem::path selPath = std::filesystem::u8path(m_SelectedFile);
        std::string filename = selPath.filename().u8string();
        std::string ext = selPath.extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

        ImGui::Spacing();
        if (IsModelExtension(ext)) {
            if (ext == ".obj") {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[3D 模型文件] (Wavefront OBJ)");
            } else if (ext == ".glb") {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[3D 模型文件] (glTF Binary .glb)");
            } else {
                ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "[3D 模型文件] (glTF Model .gltf)");
            }
            ImGui::Text("文件名: %s", filename.c_str());
            try {
                auto sz = std::filesystem::file_size(selPath);
                if (sz < 1024 * 1024) ImGui::Text("大小: %.1f KB", sz / 1024.0f);
                else ImGui::Text("大小: %.2f MB", sz / (1024.0f * 1024.0f));
            } catch (...) {}
            ImGui::Spacing();
            if (ImGui::Button("载入场景模型", ImVec2(160, 36))) {
                if (m_OnSelectedCallback) {
                    m_OnSelectedCallback(m_SelectedFile);
                }
            }
        } else if (ext == ".json") {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "[材质预设配置] (Material JSON)");
            ImGui::Text("文件名: %s", filename.c_str());
            ImGui::Spacing();
            if (ImGui::Button("导入此材质配置", ImVec2(160, 36))) {
                if (MaterialManager::Instance().LoadFromFile(m_SelectedFile)) {
                    PathTracerCore::ResetAccumulation();
                    LOG_I("Imported material preset: {}", m_SelectedFile);
                }
            }
        } else {
            ImGui::TextDisabled("选中的文件不是可预览的图片格式");
            ImGui::Text("文件: %s (%s)", filename.c_str(), ext.c_str());
        }
    } else {
        // Empty state
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextDisabled("<- 请在左侧列表中点击选择图片以预览");
        ImGui::Spacing();
        ImGui::TextDisabled("支持格式: PNG, JPG, JPEG, BMP, TGA, HDR, WEBP");
    }
}

} // namespace neurender
