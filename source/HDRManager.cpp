#include "HDRManager.h"
#include "Window.h"
#include "neuLog.h"

#ifdef _WIN32
#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <SDL3/SDL.h>
#endif

namespace neurender {

MonitorHDRInfo HDRManager::s_CurrentInfo;
bool HDRManager::s_Initialized = false;

void HDRManager::Init() {
    s_CurrentInfo = QueryDisplayHDRInfo();
    s_Initialized = true;
    LOG_I("HDRManager initialized: HDR Enabled = {}, Peak Luminance = {:.1f} nits, Full Frame = {:.1f} nits",
          s_CurrentInfo.isHDREnabled, s_CurrentInfo.maxLuminance, s_CurrentInfo.maxFullFrameLuminance);
}

MonitorHDRInfo HDRManager::QueryDisplayHDRInfo() {
    MonitorHDRInfo info;
#ifdef _WIN32
    HWND hwnd = nullptr;
    if (Window::GetNativeWindow()) {
        hwnd = (HWND)SDL_GetPointerProperty(
            SDL_GetWindowProperties(Window::GetNativeWindow()),
            SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    }
    HMONITOR targetMonitor = nullptr;
    if (hwnd) {
        targetMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    }

    Microsoft::WRL::ComPtr<IDXGIFactory6> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        for (UINT a = 0; SUCCEEDED(factory->EnumAdapters1(a, &adapter)); ++a) {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            for (UINT o = 0; SUCCEEDED(adapter->EnumOutputs(o, &output)); ++o) {
                Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
                if (SUCCEEDED(output.As(&output6))) {
                    DXGI_OUTPUT_DESC1 desc1;
                    if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                        // If we have a target monitor, match it; otherwise take the first output with HDR or valid luminance
                        if (!targetMonitor || desc1.Monitor == targetMonitor) {
                            info.maxLuminance = desc1.MaxLuminance > 1.0f ? desc1.MaxLuminance : 1000.0f;
                            info.minLuminance = desc1.MinLuminance;
                            info.maxFullFrameLuminance = desc1.MaxFullFrameLuminance > 1.0f ? desc1.MaxFullFrameLuminance : info.maxLuminance;
                            info.colorSpace = desc1.ColorSpace;
                            info.deviceName = desc1.DeviceName;
                            // DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 = 12
                            info.isHDREnabled = (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
                            return info;
                        }
                    }
                }
            }
        }
    }
#endif
    return info;
}

} // namespace neurender
