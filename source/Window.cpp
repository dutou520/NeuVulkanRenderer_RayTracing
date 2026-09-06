#include "Window.h"
#include "neuLog.h"
#include <imgui.h>
#include <imgui_impl_sdl3.h>

namespace neurender {

SDL_Window* Window::m_Window = nullptr;
bool Window::m_ShouldClose = false;
int Window::m_Width = 1600;
int Window::m_Height = 900;
std::function<void(int, int)> Window::m_ResizeCallback = nullptr;

void Window::Init(int width, int height, const char* title) {
    m_Width = width;
    m_Height = height;
    m_ShouldClose = false;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LOG_E("Failed to initialize SDL3: {}", SDL_GetError());
        throw std::runtime_error("SDL3 init failed");
    }

    m_Window = SDL_CreateWindow(
        title,
        m_Width,
        m_Height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY
    );

    if (!m_Window) {
        LOG_E("Failed to create SDL3 window: {}", SDL_GetError());
        SDL_Quit();
        throw std::runtime_error("Window creation failed");
    }

    LOG_I("Window created: {}x{}", m_Width, m_Height);
}

void Window::Shutdown() {
    if (m_Window) {
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
    }
    SDL_Quit();
    LOG_I("Window shut down.");
}

void Window::PollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (event.type == SDL_EVENT_QUIT) {
            m_ShouldClose = true;
        } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            if (event.window.windowID == SDL_GetWindowID(m_Window)) {
                m_ShouldClose = true;
            }
        } else if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            if (event.window.windowID == SDL_GetWindowID(m_Window)) {
                int w, h;
                SDL_GetWindowSizeInPixels(m_Window, &w, &h);
                if (w > 0 && h > 0 && (w != m_Width || h != m_Height)) {
                    m_Width = w;
                    m_Height = h;
                    if (m_ResizeCallback) {
                        m_ResizeCallback(w, h);
                    }
                }
            }
        }
    }
}

bool Window::IsMinimized() {
    if (!m_Window) return false;
    Uint32 flags = SDL_GetWindowFlags(m_Window);
    return (flags & SDL_WINDOW_MINIMIZED) != 0;
}

} // namespace neurender
