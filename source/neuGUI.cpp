#include "neuGUI.h"
#include "PathTracerCore.h"
#include "Window.h"
#include "neuLog.h"
#include "Console.h"
#include "TextureManager.h"
#include <imgui.h>
#include <imgui_internal.h>

namespace neurender {

FileBrowser EditorGUI::s_FileBrowser;
bool EditorGUI::s_ViewportHovered = false;
char EditorGUI::s_CommandInputBuffer[512] = "";
static bool s_DockSpaceInitialized = false;
static bool s_IsCustomResolution = false;
static int s_CustomWidth = 1280;
static int s_CustomHeight = 720;

static bool s_ShowViewport = true;
static bool s_ShowRenderSettings = true;
static bool s_ShowCameraSceneSettings = true;
static bool s_ShowMaterialManager = true;
static bool s_ShowFileBrowser = true;
static bool s_ShowConsoleLog = true;
static std::string s_PendingFocusWindow = "";

void EditorGUI::SetWindowFocus(const std::string& name) {
    s_PendingFocusWindow = name;
}

void EditorGUI::Init() {
    s_FileBrowser.SetFileSelectedCallback([](const std::string& path) {
        if (PathTracerCore::GetScene().LoadOBJ(path)) {
            PathTracerCore::GetCamera().ResetCornellBoxView();
            PathTracerCore::ResetAccumulation();
            LOG_I("Loaded model via FileBrowser: {}", path);
        }
    });
}

void EditorGUI::Shutdown() {
}

void EditorGUI::Render() {
    SetupDockSpace();
    RenderMenuBar();

    if (s_ShowViewport) {
        if (s_PendingFocusWindow == "渲染视口 (Viewport)") {
            ImGui::SetNextWindowFocus();
            s_PendingFocusWindow.clear();
        }
        RenderViewport();
    }
    if (s_ShowRenderSettings) {
        if (s_PendingFocusWindow == "渲染设置 (Render Settings)") {
            ImGui::SetNextWindowFocus();
            s_PendingFocusWindow.clear();
        }
        RenderRenderSettings();
    }
    if (s_ShowCameraSceneSettings) {
        if (s_PendingFocusWindow == "场景与相机 (Scene & Camera)") {
            ImGui::SetNextWindowFocus();
            s_PendingFocusWindow.clear();
        }
        RenderCameraSceneSettings();
    }
    if (s_ShowMaterialManager) {
        if (s_PendingFocusWindow == "材质管理器 (Material Manager)") {
            ImGui::SetNextWindowFocus();
            s_PendingFocusWindow.clear();
        }
        RenderMaterialManager();
    }
    if (s_ShowFileBrowser) {
        if (s_PendingFocusWindow == "文件浏览器 (File Browser)") {
            ImGui::SetNextWindowFocus();
            s_PendingFocusWindow.clear();
        }
        RenderFileBrowserPanel();
    }
    if (s_ShowConsoleLog) {
        if (s_PendingFocusWindow == "控制台与日志 (Console Log)") {
            ImGui::SetNextWindowFocus();
            s_PendingFocusWindow.clear();
        }
        RenderConsoleLog();
    }
}

void EditorGUI::SetupDockSpace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpaceWindow", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("PathTracerDockSpace");
    ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

    if (!s_DockSpaceInitialized) {
        s_DockSpaceInitialized = true;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, dockspace_flags | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.24f, nullptr, &dock_main_id);
        ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Down, 0.42f, nullptr, &dock_main_id);

        ImGui::DockBuilderDockWindow("渲染视口 (Viewport)", dock_main_id);
        ImGui::DockBuilderDockWindow("场景与相机 (Scene & Camera)", dock_id_left);
        ImGui::DockBuilderDockWindow("渲染设置 (Render Settings)", dock_id_left);
        ImGui::DockBuilderDockWindow("控制台与日志 (Console Log)", dock_id_bottom);
        ImGui::DockBuilderDockWindow("文件浏览器 (File Browser)", dock_id_bottom);
        ImGui::DockBuilderDockWindow("材质管理器 (Material Manager)", dock_id_bottom);

        ImGui::DockBuilderFinish(dockspace_id);
    }

    ImGui::End();
}

void EditorGUI::RenderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("文件 (File)")) {
            if (ImGui::MenuItem("加载康奈尔盒 (Load Cornell Box)")) {
                PathTracerCore::GetScene().LoadOBJ("resource/models/cornelbox.obj");
                PathTracerCore::GetCamera().ResetCornellBoxView();
                PathTracerCore::ResetAccumulation();
            }
            if (ImGui::MenuItem("保存截图 (Save Screenshot)")) {
                PathTracerCore::RequestScreenshot("screenshot.png");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("退出 (Exit)")) {
                Window::Close();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("相机 (Camera)")) {
            if (ImGui::MenuItem("重置为康奈尔盒视角 (Reset View)")) {
                PathTracerCore::GetCamera().ResetCornellBoxView();
                PathTracerCore::ResetAccumulation();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("材质 (Material)")) {
            if (ImGui::MenuItem("重置为默认康奈尔盒材质")) {
                MaterialManager::Instance().SetupCornellBoxDefaults();
                PathTracerCore::ResetAccumulation();
            }
            if (ImGui::MenuItem("导出材质到 JSON (Export)")) {
                MaterialManager::Instance().SaveToFile("materials.json");
            }
            if (ImGui::MenuItem("从 JSON 导入材质 (Import)")) {
                MaterialManager::Instance().LoadFromFile("materials.json");
                PathTracerCore::ResetAccumulation();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("设置 (Settings)")) {
            if (ImGui::BeginMenu("渲染分辨率 (Render Resolution)")) {
                if (ImGui::MenuItem("720p (1280x720)", nullptr, PathTracerCore::GetRenderWidth() == 1280 && PathTracerCore::GetRenderHeight() == 720 && !s_IsCustomResolution)) {
                    s_IsCustomResolution = false;
                    PathTracerCore::SetRenderResolution(1280, 720);
                }
                if (ImGui::MenuItem("1080p (1920x1080)", nullptr, PathTracerCore::GetRenderWidth() == 1920 && PathTracerCore::GetRenderHeight() == 1080 && !s_IsCustomResolution)) {
                    s_IsCustomResolution = false;
                    PathTracerCore::SetRenderResolution(1920, 1080);
                }
                if (ImGui::MenuItem("1440p (2560x1440)", nullptr, PathTracerCore::GetRenderWidth() == 2560 && PathTracerCore::GetRenderHeight() == 1440 && !s_IsCustomResolution)) {
                    s_IsCustomResolution = false;
                    PathTracerCore::SetRenderResolution(2560, 1440);
                }
                if (ImGui::MenuItem("4K (3840x2160)", nullptr, PathTracerCore::GetRenderWidth() == 3840 && PathTracerCore::GetRenderHeight() == 2160 && !s_IsCustomResolution)) {
                    s_IsCustomResolution = false;
                    PathTracerCore::SetRenderResolution(3840, 2160);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("自定义分辨率...", nullptr, s_IsCustomResolution)) {
                    s_IsCustomResolution = true;
                    s_CustomWidth = PathTracerCore::GetRenderWidth();
                    s_CustomHeight = PathTracerCore::GetRenderHeight();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("界面缩放 (UI Scale)")) {
                float scales[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
                const char* scaleNames[] = { "100%", "125%", "150%", "175%", "200%" };
                for (int i = 0; i < 5; ++i) {
                    bool selected = (std::abs(ImGui::GetIO().FontGlobalScale - scales[i]) < 0.01f);
                    if (ImGui::MenuItem(scaleNames[i], nullptr, selected)) {
                        ImGui::GetIO().FontGlobalScale = scales[i];
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("窗口 (Window)")) {
            ImGui::MenuItem("渲染视口 (Viewport)", nullptr, &s_ShowViewport);
            ImGui::MenuItem("渲染设置 (Render Settings)", nullptr, &s_ShowRenderSettings);
            ImGui::MenuItem("场景与相机 (Scene & Camera)", nullptr, &s_ShowCameraSceneSettings);
            ImGui::Separator();
            ImGui::MenuItem("材质管理器 (Material Manager)", nullptr, &s_ShowMaterialManager);
            ImGui::MenuItem("文件浏览器 (File Browser)", nullptr, &s_ShowFileBrowser);
            ImGui::MenuItem("控制台与日志 (Console Log)", nullptr, &s_ShowConsoleLog);
            ImGui::Separator();
            if (ImGui::MenuItem("重置默认窗口布局 (Reset Layout)")) {
                s_DockSpaceInitialized = false;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("帮助 (Help)")) {
            if (ImGui::MenuItem("关于 (About)")) {
                LOG_I("NeuTracingRender: Vulkan Real-Time Path Tracer Demo.");
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorGUI::RenderViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("渲染视口 (Viewport)", &s_ShowViewport, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 viewSize = ImGui::GetContentRegionAvail();
    ImTextureID texID = PathTracerCore::GetViewportTextureID();
    if (texID && viewSize.x > 32 && viewSize.y > 32) {
        float renderAspect = static_cast<float>(PathTracerCore::GetRenderWidth()) / static_cast<float>(PathTracerCore::GetRenderHeight());
        float viewAspect = viewSize.x / viewSize.y;
        ImVec2 drawSize;
        if (viewAspect > renderAspect) {
            drawSize.y = viewSize.y;
            drawSize.x = viewSize.y * renderAspect;
        } else {
            drawSize.x = viewSize.x;
            drawSize.y = viewSize.x / renderAspect;
        }
        ImVec2 curPos = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(curPos.x + (viewSize.x - drawSize.x) * 0.5f, curPos.y + (viewSize.y - drawSize.y) * 0.5f));
        ImGui::Image(texID, drawSize);
    }

    s_ViewportHovered = ImGui::IsItemHovered();

    // Mouse & Keyboard Camera Navigation in Viewport
    if (s_ViewportHovered) {
        auto& cam = PathTracerCore::GetCamera();
        ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            cam.ProcessMouseMovement(io.MouseDelta.x, -io.MouseDelta.y);

            float dt = io.DeltaTime;
            if (ImGui::IsKeyDown(ImGuiKey_W)) cam.ProcessKeyboard(CameraMovement::Forward, dt);
            if (ImGui::IsKeyDown(ImGuiKey_S)) cam.ProcessKeyboard(CameraMovement::Backward, dt);
            if (ImGui::IsKeyDown(ImGuiKey_A)) cam.ProcessKeyboard(CameraMovement::Left, dt);
            if (ImGui::IsKeyDown(ImGuiKey_D)) cam.ProcessKeyboard(CameraMovement::Right, dt);
            if (ImGui::IsKeyDown(ImGuiKey_E)) cam.ProcessKeyboard(CameraMovement::Up, dt);
            if (ImGui::IsKeyDown(ImGuiKey_Q)) cam.ProcessKeyboard(CameraMovement::Down, dt);
        }

        if (io.MouseWheel != 0.0f) {
            cam.ProcessMouseScroll(io.MouseWheel);
        }
    }

    // Overlay stats in viewport
    ImGui::SetCursorPos(ImVec2(20, 30));
    ImGui::BeginChild("StatsOverlay", ImVec2(240, 130), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::Text("SPP: %d / %d", PathTracerCore::GetAccumulatedSPP(), PathTracerCore::GetTargetSPP());
    ImGui::Text("分辨率: %dx%d", PathTracerCore::GetRenderWidth(), PathTracerCore::GetRenderHeight());
    ImGui::Text("帧率: %.1f FPS (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::Text("三角面数: %zu", PathTracerCore::GetScene().GetTriangles().size());
    if (PathTracerCore::GetBloomEnabled()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Bloom: 已开启 (强度 %.1f)", PathTracerCore::GetBloomIntensity());
    } else {
        ImGui::TextDisabled("Bloom: 已关闭");
    }
    ImGui::TextDisabled("按住右键 + WASD 漫游视角");
    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorGUI::RenderRenderSettings() {
    ImGui::Begin("渲染设置 (Render Settings)", &s_ShowRenderSettings);

    // SPP Progress
    int spp = PathTracerCore::GetAccumulatedSPP();
    int targetSPP = PathTracerCore::GetTargetSPP();
    float progress = (targetSPP > 0) ? std::min(1.0f, static_cast<float>(spp) / targetSPP) : 0.0f;
    char progBuf[64];
    snprintf(progBuf, sizeof(progBuf), "%d / %d SPP (%.1f%%)", spp, targetSPP, progress * 100.0f);
    ImGui::ProgressBar(progress, ImVec2(-1, 0), progBuf);

    if (ImGui::Button("重置累积 (Reset Accumulation)", ImVec2(-1, 30))) {
        PathTracerCore::ResetAccumulation();
    }

    ImGui::Separator();
    // Resolution Section
    ImGui::Text("渲染分辨率 (Render Resolution): %dx%d", PathTracerCore::GetRenderWidth(), PathTracerCore::GetRenderHeight());
    const char* resPresets[] = { "720p (1280x720)", "1080p (1920x1080)", "1440p 2K (2560x1440)", "4K (3840x2160)", "自定义 (Custom)" };
    static int currentResIndex = 0;

    uint32_t rw = PathTracerCore::GetRenderWidth();
    uint32_t rh = PathTracerCore::GetRenderHeight();

    if (!s_IsCustomResolution) {
        if (rw == 1280 && rh == 720) currentResIndex = 0;
        else if (rw == 1920 && rh == 1080) currentResIndex = 1;
        else if (rw == 2560 && rh == 1440) currentResIndex = 2;
        else if (rw == 3840 && rh == 2160) currentResIndex = 3;
        else {
            currentResIndex = 4;
            s_IsCustomResolution = true;
            s_CustomWidth = static_cast<int>(rw);
            s_CustomHeight = static_cast<int>(rh);
        }
    }

    if (ImGui::Combo("分辨率预设", &currentResIndex, resPresets, 5)) {
        if (currentResIndex == 0) {
            s_IsCustomResolution = false;
            PathTracerCore::SetRenderResolution(1280, 720);
        } else if (currentResIndex == 1) {
            s_IsCustomResolution = false;
            PathTracerCore::SetRenderResolution(1920, 1080);
        } else if (currentResIndex == 2) {
            s_IsCustomResolution = false;
            PathTracerCore::SetRenderResolution(2560, 1440);
        } else if (currentResIndex == 3) {
            s_IsCustomResolution = false;
            PathTracerCore::SetRenderResolution(3840, 2160);
        } else if (currentResIndex == 4) {
            s_IsCustomResolution = true;
            s_CustomWidth = static_cast<int>(rw);
            s_CustomHeight = static_cast<int>(rh);
        }
    }

    if (s_IsCustomResolution || currentResIndex == 4) {
        ImGui::PushID("CustomResolutionInputs");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "自定义分辨率设置:");
        ImGui::InputInt("宽 (Width)", &s_CustomWidth, 64, 256);
        ImGui::InputInt("高 (Height)", &s_CustomHeight, 64, 256);
        if (s_CustomWidth < 64) s_CustomWidth = 64;
        if (s_CustomHeight < 64) s_CustomHeight = 64;
        if (s_CustomWidth > 7680) s_CustomWidth = 7680;
        if (s_CustomHeight > 4320) s_CustomHeight = 4320;
        if (ImGui::Button("应用自定义分辨率 (Apply)", ImVec2(-1, 30))) {
            PathTracerCore::SetRenderResolution(static_cast<uint32_t>(s_CustomWidth), static_cast<uint32_t>(s_CustomHeight));
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::Text("采样与光线 (Sampling & Rays)");

    int target = targetSPP;
    if (ImGui::SliderInt("目标 SPP (0=无限)", &target, 0, 4096)) {
        PathTracerCore::SetTargetSPP(target);
    }

    int spf = PathTracerCore::GetSamplesPerFrame();
    if (ImGui::SliderInt("单帧采样数 (SPF)", &spf, 1, 16)) {
        PathTracerCore::SetSamplesPerFrame(spf);
    }

    int bounces = PathTracerCore::GetMaxBounces();
    if (ImGui::SliderInt("最大反弹次数", &bounces, 1, 16)) {
        PathTracerCore::SetMaxBounces(bounces);
    }

    // Bloom Section
    ImGui::Separator();
    if (ImGui::CollapsingHeader("泛光效果 (Bloom) [实时生效]", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool bloomOn = PathTracerCore::GetBloomEnabled();
        if (ImGui::Checkbox("启用 Bloom 泛光", &bloomOn)) {
            PathTracerCore::SetBloomEnabled(bloomOn);
        }

        if (bloomOn) {
            float bThresh = PathTracerCore::GetBloomThreshold();
            if (ImGui::SliderFloat("阈值 (Threshold)", &bThresh, 0.1f, 5.0f, "%.2f")) {
                PathTracerCore::SetBloomThreshold(bThresh);
            }

            float bSoft = PathTracerCore::GetBloomSoftThreshold();
            if (ImGui::SliderFloat("软过渡 (Soft Knee)", &bSoft, 0.0f, 2.0f, "%.2f")) {
                PathTracerCore::SetBloomSoftThreshold(bSoft);
            }

            float bIntensity = PathTracerCore::GetBloomIntensity();
            if (ImGui::SliderFloat("泛光强度 (Intensity)", &bIntensity, 0.0f, 3.0f, "%.2f")) {
                PathTracerCore::SetBloomIntensity(bIntensity);
            }

            float bRadius = PathTracerCore::GetBloomRadius();
            if (ImGui::SliderFloat("散射半径 (Radius)", &bRadius, 0.2f, 3.0f, "%.2f")) {
                PathTracerCore::SetBloomRadius(bRadius);
            }
        }
    }

    // Denoiser Section
    ImGui::Separator();
    if (ImGui::CollapsingHeader("智能降噪 (Denoiser) [实时生效]", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool denoiseOn = PathTracerCore::GetDenoiserEnabled();
        if (ImGui::Checkbox("启用边缘感知降噪 (Enable Denoiser)", &denoiseOn)) {
            PathTracerCore::SetDenoiserEnabled(denoiseOn);
        }

        if (denoiseOn) {
            float cSigma = PathTracerCore::GetDenoiserColorSigma();
            if (ImGui::SliderFloat("色彩容差 (Color Sigma)", &cSigma, 0.01f, 0.5f, "%.3f")) {
                PathTracerCore::SetDenoiserColorSigma(cSigma);
            }

            float nSigma = PathTracerCore::GetDenoiserNormalSigma();
            if (ImGui::SliderFloat("法线敏感度 (Normal Sigma)", &nSigma, 4.0f, 64.0f, "%.1f")) {
                PathTracerCore::SetDenoiserNormalSigma(nSigma);
            }

            float dSigma = PathTracerCore::GetDenoiserDepthSigma();
            if (ImGui::SliderFloat("深度敏感度 (Depth Sigma)", &dSigma, 0.005f, 0.2f, "%.3f")) {
                PathTracerCore::SetDenoiserDepthSigma(dSigma);
            }
        }
    }

    // PostProcess Tone & Color
    ImGui::Separator();
    ImGui::Text("色调映射与后处理 (Color & Tone) [实时生效]");

    const char* tonemapModes[] = { "ACES", "Reinhard", "Linear" };
    int tm = PathTracerCore::GetTonemapMode();
    if (ImGui::Combo("色调映射模式", &tm, tonemapModes, 3)) {
        PathTracerCore::SetTonemapMode(tm);
    }

    float exposure = PathTracerCore::GetExposure();
    if (ImGui::SliderFloat("曝光度 (Exposure)", &exposure, 0.05f, 5.0f, "%.2f")) {
        PathTracerCore::SetExposure(exposure);
    }

    float gamma = PathTracerCore::GetGamma();
    if (ImGui::SliderFloat("Gamma 校正", &gamma, 1.0f, 3.0f, "%.2f")) {
        PathTracerCore::SetGamma(gamma);
    }

    ImGui::Separator();
    if (ImGui::Button("截取并保存图片 (Screenshot)", ImVec2(-1, 30))) {
        PathTracerCore::RequestScreenshot("screenshot.png");
    }

    ImGui::End();
}

void EditorGUI::RenderCameraSceneSettings() {
    ImGui::Begin("场景与相机 (Scene & Camera)", &s_ShowCameraSceneSettings);

    auto& cam = PathTracerCore::GetCamera();

    if (ImGui::CollapsingHeader("相机参数 (Camera)", ImGuiTreeNodeFlags_DefaultOpen)) {
        glm::vec3 pos = cam.GetPosition();
        if (ImGui::DragFloat3("位置 (Pos)", &pos.x, 0.05f)) {
            cam.SetPosition(pos);
        }

        glm::vec3 target = cam.GetTarget();
        if (ImGui::DragFloat3("目标 (Target)", &target.x, 0.05f)) {
            cam.SetTarget(target);
        }

        float fov = cam.GetFov();
        if (ImGui::SliderFloat("视场角 (FOV)", &fov, 10.0f, 120.0f, "%.1f deg")) {
            cam.SetFov(fov);
        }

        float aperture = cam.GetAperture();
        if (ImGui::SliderFloat("光圈半径 (Aperture)", &aperture, 0.0f, 0.1f, "%.4f")) {
            cam.SetAperture(aperture);
        }

        float focusDist = cam.GetFocusDistance();
        if (ImGui::SliderFloat("对焦距离 (Focus Dist)", &focusDist, 0.1f, 10.0f, "%.2f")) {
            cam.SetFocusDistance(focusDist);
        }

        if (ImGui::Button("重置为康奈尔盒视角", ImVec2(-1, 0))) {
            cam.ResetCornellBoxView();
            PathTracerCore::ResetAccumulation();
        }
    }

    if (ImGui::CollapsingHeader("天空与环境光 (Sky & Environment)", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* skyModes[] = { "恒定背景色 (Constant Color)", "Nishita 1993 物理大气模型 (Physical Atmosphere)" };
        int skyMode = PathTracerCore::GetSkyMode();
        if (ImGui::Combo("天空模式", &skyMode, skyModes, 2)) {
            PathTracerCore::SetSkyMode(skyMode);
        }

        if (skyMode == 0) {
            glm::vec3 envCol = PathTracerCore::GetEnvColor();
            if (ImGui::ColorEdit3("环境光颜色", &envCol.x)) {
                PathTracerCore::SetEnvColor(envCol);
            }

            float envInt = PathTracerCore::GetEnvIntensity();
            if (ImGui::SliderFloat("环境光强度", &envInt, 0.0f, 5.0f, "%.2f")) {
                PathTracerCore::SetEnvIntensity(envInt);
            }
        } else {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Nishita 1993 物理大气参数:");

            float el = PathTracerCore::GetSunElevation();
            if (ImGui::SliderFloat("太阳高度角 (Elevation)", &el, -5.0f, 90.0f, "%.1f deg")) {
                PathTracerCore::SetSunElevation(el);
            }

            float az = PathTracerCore::GetSunAzimuth();
            if (ImGui::SliderFloat("太阳方位角 (Azimuth)", &az, 0.0f, 360.0f, "%.1f deg")) {
                PathTracerCore::SetSunAzimuth(az);
            }

            float intensity = PathTracerCore::GetSunIntensity();
            if (ImGui::SliderFloat("太阳光照强度", &intensity, 0.1f, 50.0f, "%.1f")) {
                PathTracerCore::SetSunIntensity(intensity);
            }

            float rScale = PathTracerCore::GetRayleighScale();
            if (ImGui::SliderFloat("瑞利散射 (Rayleigh)", &rScale, 0.1f, 5.0f, "%.2f")) {
                PathTracerCore::SetRayleighScale(rScale);
            }

            float mTurb = PathTracerCore::GetMieTurbidity();
            if (ImGui::SliderFloat("气溶胶浊度 (Mie Turbidity)", &mTurb, 0.0f, 10.0f, "%.2f")) {
                PathTracerCore::SetMieTurbidity(mTurb);
            }

            float sunSize = PathTracerCore::GetSunAngularSize();
            if (ImGui::SliderFloat("太阳圆盘大小", &sunSize, 0.002f, 0.05f, "%.4f rad")) {
                PathTracerCore::SetSunAngularSize(sunSize);
            }

            glm::vec3 ground = PathTracerCore::GetGroundAlbedo();
            if (ImGui::ColorEdit3("地表反照率 (Ground)", &ground.x)) {
                PathTracerCore::SetGroundAlbedo(ground);
            }
        }
    }

    if (ImGui::CollapsingHeader("场景物体列表 (Scene Objects)", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& scene = PathTracerCore::GetScene();
        auto& objects = scene.GetObjects();
        auto& matMgr = MaterialManager::Instance();

        if (objects.empty()) {
            ImGui::TextDisabled("未加载物体");
        } else {
            for (size_t i = 0; i < objects.size(); ++i) {
                auto& obj = objects[i];
                ImGui::PushID(static_cast<int>(i));

                Material* curMat = matMgr.GetMaterial(obj.materialId);
                std::string matName = curMat ? curMat->name : "None";

                ImGui::Text("%zu. %s", i + 1, obj.name.c_str());
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140);
                if (ImGui::BeginCombo("##ObjMat", matName.c_str())) {
                    for (int m = 0; m < matMgr.GetCount(); ++m) {
                        bool isSel = (obj.materialId == m);
                        if (ImGui::Selectable(matMgr.GetMaterial(m)->name.c_str(), isSel)) {
                            scene.SetObjectMaterial(static_cast<int>(i), m);
                            PathTracerCore::ResetAccumulation();
                        }
                        if (isSel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
        }
    }

    ImGui::End();
}

void EditorGUI::RenderMaterialManager() {
    if (s_PendingFocusWindow == "材质管理器 (Material Manager)") {
        ImGui::SetNextWindowFocus();
        s_PendingFocusWindow.clear();
    }
    if (!ImGui::Begin("材质管理器 (Material Manager)", &s_ShowMaterialManager)) {
        ImGui::End();
        return;
    }

    auto& matMgr = MaterialManager::Instance();
    int selIdx = matMgr.GetSelectedIndex();

    ImGui::Columns(2, "MatColumns", true);
    ImGui::SetColumnWidth(0, 240);

    // Left: Material List
    ImGui::Text("材质列表 (%d)", matMgr.GetCount());
    ImGui::BeginChild("MatList", ImVec2(0, -35), true);
    for (int i = 0; i < matMgr.GetCount(); ++i) {
        const auto* m = matMgr.GetMaterial(i);
        ImGui::PushID(i);

        // Color badge
        ImVec4 col(m->albedo.r, m->albedo.g, m->albedo.b, 1.0f);
        if (m->type == MaterialType::Emissive) {
            col = ImVec4(m->emission.r, m->emission.g, m->emission.b, 1.0f);
        }
        ImGui::ColorButton("##badge", col, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
        ImGui::SameLine();

        bool isSelected = (i == selIdx);
        if (ImGui::Selectable(m->name.c_str(), isSelected)) {
            matMgr.SetSelectedIndex(i);
            selIdx = i;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button("+ 新建")) {
        Material newMat;
        newMat.name = "New Material " + std::to_string(matMgr.GetCount());
        matMgr.AddMaterial(newMat);
    }
    ImGui::SameLine();
    if (ImGui::Button("复制") && selIdx >= 0) {
        Material copy = *matMgr.GetMaterial(selIdx);
        copy.name += " (Copy)";
        matMgr.AddMaterial(copy);
    }
    ImGui::SameLine();
    if (ImGui::Button("- 删除") && matMgr.GetCount() > 1) {
        matMgr.RemoveMaterial(selIdx);
    }

    ImGui::NextColumn();

    // Right: Material Properties Editor
    Material* mat = matMgr.GetMaterial(selIdx);
    if (!mat) {
        ImGui::TextDisabled("请选择一个材质以编辑属性");
        ImGui::Columns(1);
        ImGui::End();
        return;
    }

    ImGui::Text("正在编辑: %s", mat->name.c_str());
    ImGui::Separator();
    ImGui::BeginChild("MatPropScroll", ImVec2(0, 0), false);

    char nameBuf[128];
    strncpy(nameBuf, mat->name.c_str(), sizeof(nameBuf));
    if (ImGui::InputText("材质名称", nameBuf, sizeof(nameBuf))) {
        mat->name = nameBuf;
    }

    const char* typeNames[] = { "0: 漫反射 (Diffuse)", "1: 金属 (Metal)", "2: 玻璃/绝缘体 (Glass)", "3: 发光 (Emissive)" };
    int curType = static_cast<int>(mat->type);
    if (ImGui::Combo("材质类型", &curType, typeNames, 4)) {
        mat->type = static_cast<MaterialType>(curType);
        matMgr.SetDirty(true);
        PathTracerCore::ResetAccumulation();
    }

    if (ImGui::ColorEdit3("基础颜色 (Albedo)", &mat->albedo.r)) {
        matMgr.SetDirty(true);
        PathTracerCore::ResetAccumulation();
    }

    if (mat->type == MaterialType::Diffuse || mat->type == MaterialType::Metal) {
        if (ImGui::SliderFloat("粗糙度 (Roughness)", &mat->roughness, 0.0f, 1.0f)) {
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
    }

    if (mat->type == MaterialType::Metal) {
        if (ImGui::SliderFloat("金属度 (Metallic)", &mat->metallic, 0.0f, 1.0f)) {
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
    }

    if (mat->type == MaterialType::Glass) {
        if (ImGui::SliderFloat("折射率 (IOR)", &mat->ior, 1.0f, 2.5f, "%.2f")) {
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
        if (ImGui::SliderFloat("透射度 (Transmission)", &mat->transmission, 0.0f, 1.0f)) {
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
    }

    if (mat->type == MaterialType::Emissive) {
        if (ImGui::ColorEdit3("发光颜色 (Emission)", &mat->emission.r)) {
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
        if (ImGui::DragFloat("发光强度 (Intensity)", &mat->emissionIntensity, 0.2f, 0.0f, 100.0f, "%.1f")) {
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "纹理与贴图 (Textures & Maps):");

    const std::string& selectedBrowserFile = s_FileBrowser.GetSelectedFile();

    // Roughness Texture
    ImGui::Text("粗糙度贴图 (Roughness Map):");
    if (ImGui::Button("加载当前选中的纹理##LoadRoughFromSel")) {
        if (!selectedBrowserFile.empty()) {
            mat->roughnessTexPath = selectedBrowserFile;
            mat->roughnessTexIdx = TextureManager::Instance().LoadTexture(mat->roughnessTexPath);
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
    }
    if (!selectedBrowserFile.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("已选: %s", selectedBrowserFile.c_str());
    }

    char roughBuf[256];
    strncpy(roughBuf, mat->roughnessTexPath.c_str(), sizeof(roughBuf));
    if (ImGui::InputText("粗糙度贴图路径", roughBuf, sizeof(roughBuf))) {
        mat->roughnessTexPath = roughBuf;
    }
    ImGui::SameLine();
    if (ImGui::Button("加载##LoadRough")) {
        if (!mat->roughnessTexPath.empty()) {
            mat->roughnessTexIdx = TextureManager::Instance().LoadTexture(mat->roughnessTexPath);
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("清除##ClearRough")) {
        mat->roughnessTexPath.clear();
        mat->roughnessTexIdx = -1;
        matMgr.SetDirty(true);
        PathTracerCore::ResetAccumulation();
    }
    if (mat->roughnessTexIdx >= 0) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "粗糙度贴图已绑定至槽位: %d", mat->roughnessTexIdx);
        if (ImGui::SliderFloat("粗糙度贴图强度", &mat->roughnessScale, 0.0f, 2.0f, "%.2f")) {
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
    }

    ImGui::Spacing();

    // Normal Texture
    ImGui::Text("法线贴图 (Normal Map):");
    if (ImGui::Button("加载当前选中的纹理##LoadNormFromSel")) {
        if (!selectedBrowserFile.empty()) {
            mat->normalTexPath = selectedBrowserFile;
            mat->normalTexIdx = TextureManager::Instance().LoadTexture(mat->normalTexPath);
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
    }
    if (!selectedBrowserFile.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("已选: %s", selectedBrowserFile.c_str());
    }

    char normBuf[256];
    strncpy(normBuf, mat->normalTexPath.c_str(), sizeof(normBuf));
    if (ImGui::InputText("法线贴图路径", normBuf, sizeof(normBuf))) {
        mat->normalTexPath = normBuf;
    }
    ImGui::SameLine();
    if (ImGui::Button("加载##LoadNorm")) {
        if (!mat->normalTexPath.empty()) {
            mat->normalTexIdx = TextureManager::Instance().LoadTexture(mat->normalTexPath);
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("清除##ClearNorm")) {
        mat->normalTexPath.clear();
        mat->normalTexIdx = -1;
        matMgr.SetDirty(true);
        PathTracerCore::ResetAccumulation();
    }
    if (mat->normalTexIdx >= 0) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "法线贴图已绑定至槽位: %d", mat->normalTexIdx);
        if (ImGui::SliderFloat("法线扰动强度 (Normal Scale)", &mat->normalScale, 0.0f, 3.0f, "%.2f")) {
            matMgr.SetDirty(true);
            PathTracerCore::ResetAccumulation();
        }
    }

    ImGui::Spacing();
    ImGui::Text("常用预设:");
    if (ImGui::Button("黄金 (Gold)")) {
        mat->name = "Gold";
        mat->type = MaterialType::Metal;
        mat->albedo = glm::vec3(1.0f, 0.766f, 0.336f);
        mat->metallic = 0.95f;
        mat->roughness = 0.1f;
        matMgr.SetDirty(true);
        PathTracerCore::ResetAccumulation();
    }
    ImGui::SameLine();
    if (ImGui::Button("玻璃 (Glass)")) {
        mat->name = "Glass";
        mat->type = MaterialType::Glass;
        mat->albedo = glm::vec3(1.0f);
        mat->ior = 1.52f;
        mat->transmission = 1.0f;
        mat->roughness = 0.0f;
        matMgr.SetDirty(true);
        PathTracerCore::ResetAccumulation();
    }
    ImGui::SameLine();
    if (ImGui::Button("红漫反射 (Red)")) {
        mat->name = "Cornell Red";
        mat->type = MaterialType::Diffuse;
        mat->albedo = glm::vec3(0.63f, 0.065f, 0.05f);
        mat->roughness = 0.5f;
        matMgr.SetDirty(true);
        PathTracerCore::ResetAccumulation();
    }
    ImGui::SameLine();
    if (ImGui::Button("绿漫反射 (Green)")) {
        mat->name = "Cornell Green";
        mat->type = MaterialType::Diffuse;
        mat->albedo = glm::vec3(0.14f, 0.45f, 0.091f);
        mat->roughness = 0.5f;
        matMgr.SetDirty(true);
        PathTracerCore::ResetAccumulation();
    }
    ImGui::SameLine();
    if (ImGui::Button("顶灯 (Ceiling Light)")) {
        mat->name = "Ceiling Light";
        mat->type = MaterialType::Emissive;
        mat->albedo = glm::vec3(1.0f);
        mat->emission = glm::vec3(1.0f, 0.95f, 0.85f);
        mat->emissionIntensity = 15.0f;
        matMgr.SetDirty(true);
        PathTracerCore::ResetAccumulation();
    }

    ImGui::EndChild();
    ImGui::Columns(1);
    ImGui::End();
}

void EditorGUI::RenderFileBrowserPanel() {
    if (s_PendingFocusWindow == "文件浏览器 (File Browser)") {
        ImGui::SetNextWindowFocus();
        s_PendingFocusWindow.clear();
    }
    if (!ImGui::Begin("文件浏览器 (File Browser)", &s_ShowFileBrowser)) {
        ImGui::End();
        return;
    }
    s_FileBrowser.Render();
    ImGui::End();
}

void EditorGUI::RenderConsoleLog() {
    if (s_PendingFocusWindow == "控制台与日志 (Console Log)") {
        ImGui::SetNextWindowFocus();
        s_PendingFocusWindow.clear();
    }
    if (!ImGui::Begin("控制台与日志 (Console Log)", &s_ShowConsoleLog)) {
        ImGui::End();
        return;
    }

    auto logs = NeuLog::GetRecentLogs();

    if (ImGui::Button("清空日志 (Clear)")) {
        NeuLog::ClearLogs();
    }
    ImGui::SameLine();
    ImGui::Text("日志行数: %zu", logs.size());

    ImGui::BeginChild("LogScrollRegion", ImVec2(0, -35), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    for (const auto& item : logs) {
        int level = item.second;
        ImVec4 col(0.8f, 0.8f, 0.8f, 1.0f);
        if (level == spdlog::level::warn) col = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        else if (level == spdlog::level::err || level == spdlog::level::critical) col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        else if (level == spdlog::level::info) col = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(item.first.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    // Command Input
    ImGui::PushItemWidth(-80);
    bool enterPressed = ImGui::InputText("##CmdInput", s_CommandInputBuffer, sizeof(s_CommandInputBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if ((ImGui::Button("执行 (Run)", ImVec2(70, 0)) || enterPressed) && s_CommandInputBuffer[0] != '\0') {
        Console::Execute(s_CommandInputBuffer);
        s_CommandInputBuffer[0] = '\0';
    }

    ImGui::End();
}

} // namespace neurender
