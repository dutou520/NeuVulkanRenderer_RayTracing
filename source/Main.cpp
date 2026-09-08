#include "Window.h"
#include "PathTracerCore.h"
#include "neuGUI.h"
#include "Console.h"
#include "neuLog.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <algorithm>
#include <cctype>

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#endif

void setConsoleUtf8Encoding() {
#if defined(_WIN32) || defined(_WIN64)
    SetConsoleOutputCP(CP_UTF8);
#endif
}

int main(int argc, char* argv[]) {
    setConsoleUtf8Encoding();

    try {
        std::locale::global(std::locale(".UTF8"));
    } catch (...) {}

    neurender::NeuLog::Init();
    LOG_I("Starting NeuTracingRender (Vulkan Path Tracer)...");

    try {
        neurender::Window::Init(1600, 900, "NeuTracingRender — Vulkan 路径追踪渲染器");

        neurender::MaterialManager::Instance().Init();
        neurender::PathTracerCore::Init();
        neurender::EditorGUI::Init();

        // Load Cornell Box by default
        std::string startupModel = "resource/models/康奈尔盒子.obj";
        if (argc > 1) {
            startupModel = argv[1];
        }

        LOG_I("Loading initial model: {}", startupModel);
        if (neurender::PathTracerCore::GetScene().LoadModel(startupModel)) {
            std::string lower = startupModel;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
            if (lower.find("康奈尔") != std::string::npos || lower.find("cornell") != std::string::npos) {
                neurender::PathTracerCore::GetCamera().ResetCornellBoxView();
            } else {
                neurender::PathTracerCore::GetCamera().FrameBounds(neurender::PathTracerCore::GetScene().GetBounds());
            }
            neurender::PathTracerCore::ResetAccumulation();
            LOG_I("Initial model loaded successfully.");
        } else {
            LOG_W("Could not load model at startup: {}", startupModel);
        }

        neurender::Console::Init();

        // Main Loop
        while (!neurender::Window::ShouldClose()) {
            neurender::Window::PollEvents();
            if (neurender::Window::ShouldClose()) {
                break;
            }

            if (neurender::Window::IsMinimized()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            neurender::Console::Update();
            neurender::PathTracerCore::ProcessPendingResize();

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            neurender::EditorGUI::Render();

            ImGui::Render();
            neurender::PathTracerCore::RenderFrame();
        }

        neurender::Console::Shutdown();
        neurender::EditorGUI::Shutdown();
        neurender::PathTracerCore::Shutdown();
        neurender::Window::Shutdown();
        neurender::NeuLog::Shutdown();

    } catch (const std::exception& e) {
        LOG_E("Fatal error: {}", e.what());
        return -1;
    }

    return 0;
}
