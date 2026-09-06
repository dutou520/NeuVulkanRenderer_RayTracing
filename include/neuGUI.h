#pragma once
#include <string>
#include <memory>
#include "FileBrowser.h"

namespace neurender {

class EditorGUI {
public:
    static void Init();
    static void Render();
    static void Shutdown();

    static void SetWindowFocus(const std::string& name);

    static FileBrowser& GetFileBrowser() { return s_FileBrowser; }

private:
    static void SetupDockSpace();
    static void RenderMenuBar();
    static void RenderViewport();
    static void RenderRenderSettings();
    static void RenderCameraSceneSettings();
    static void RenderMaterialManager();
    static void RenderFileBrowserPanel();
    static void RenderConsoleLog();

    static FileBrowser s_FileBrowser;
    static bool s_ViewportHovered;
    static char s_CommandInputBuffer[512];
};

} // namespace neurender
