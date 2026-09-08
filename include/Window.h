#pragma once
#include <SDL3/SDL.h>
#include <functional>

namespace neurender {

class Window {
public:
    Window() = delete;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    static void Init(int width, int height, const char* title);
    static void Shutdown();
    static void PollEvents();
    static bool ShouldClose() { return m_ShouldClose; }
    static void Close();

    static SDL_Window* GetNativeWindow() { return m_Window; }
    static int GetWidth() { return m_Width; }
    static int GetHeight() { return m_Height; }
    static bool IsMinimized();

    static void SetResizeCallback(std::function<void(int, int)> cb) { m_ResizeCallback = cb; }

private:
    static SDL_Window* m_Window;
    static bool m_ShouldClose;
    static int m_Width;
    static int m_Height;
    static std::function<void(int, int)> m_ResizeCallback;
};

} // namespace neurender
