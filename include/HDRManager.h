#pragma once
#include <string>

namespace neurender {

struct MonitorHDRInfo {
    bool isHDREnabled = false;
    float maxLuminance = 1000.0f;          // Peak luminance in nits
    float minLuminance = 0.001f;           // Min luminance in nits
    float maxFullFrameLuminance = 800.0f;  // Full frame max luminance in nits
    int colorSpace = 0;                    // DXGI_COLOR_SPACE_TYPE
    std::wstring deviceName;
};

enum class HDROutputMode {
    Auto = 0,             // Auto negotiate: ScRGB -> HDR10 -> SDR
    Force_ScRGB = 1,      // VK_FORMAT_R16G16B16A16_SFLOAT + EXTENDED_SRGB_LINEAR
    Force_HDR10 = 2,      // VK_FORMAT_A2B10G10R10_UNORM_PACK32 + HDR10_ST2084
    Disabled_SDR = 3      // VK_FORMAT_B8G8R8A8_UNORM + SRGB_NONLINEAR
};

class HDRManager {
public:
    static void Init();
    static MonitorHDRInfo QueryDisplayHDRInfo();
    static const MonitorHDRInfo& GetCurrentInfo() { return s_CurrentInfo; }

private:
    static MonitorHDRInfo s_CurrentInfo;
    static bool s_Initialized;
};

} // namespace neurender
