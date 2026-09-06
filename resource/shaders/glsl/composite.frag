#version 450
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D s_HDRViewport;
layout(binding = 1) uniform sampler2D s_SDRUI;

layout(push_constant) uniform PushConstants {
    vec4 viewportRect;   // xy = minUV (minX/winW, minY/winH), zw = maxUV (maxX/winW, maxY/winH)
    vec4 hdrParams;      // x = paperWhiteScale (paperWhite / 80.0), y = outputMode (0=SDR, 1=ScRGB, 2=HDR10), z = peakNits, w = softKneeThreshold
    vec4 displayParams;  // x = tonemapMode, y = gamma, z = exposure, w = reserved
} pc;

// Rec.709 to Rec.2020 color space transformation matrix
const mat3 Rec709To2020 = mat3(
    0.6274040, 0.0690970, 0.0163916,
    0.3292820, 0.9195400, 0.0880132,
    0.0433136, 0.0113612, 0.8955950
);

// SMPTE ST 2084 (PQ Curve) encode function
vec3 LinearToPQ(vec3 L) {
    const float m1 = 2610.0 / 4096.0 / 4.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;

    vec3 Lm = pow(clamp(L, 0.0, 1.0), vec3(m1));
    return pow((c1 + c2 * Lm) / (1.0 + c3 * Lm), vec3(m2));
}

void main() {
    vec2 uv = inUV;

    // 1. Sample SDR UI (rendered by ImGui in sRGB)
    vec4 ui = texture(s_SDRUI, uv);
    vec3 uiLinear = pow(max(ui.rgb, vec3(0.0)), vec3(2.2));

    // 2. Check if uv is inside Viewport rect
    bool inViewport = (uv.x >= pc.viewportRect.x && uv.x <= pc.viewportRect.z &&
                       uv.y >= pc.viewportRect.y && uv.y <= pc.viewportRect.w);

    int mode = int(pc.hdrParams.y + 0.5);

    if (inViewport) {
        vec2 vRange = max(pc.viewportRect.zw - pc.viewportRect.xy, vec2(1e-5));
        vec2 vUV = (uv - pc.viewportRect.xy) / vRange;
        vec3 sceneColor = texture(s_HDRViewport, vUV).rgb;

        // Viewport UI Overlap Logic:
        // Inside viewport, the scene is the baseline layer.
        // Pure black/very dark background clears or DockNode backgrounds (uiBrightness < 0.03)
        // must NEVER attenuate the 3D scene. Only actual UI elements (StatsOverlay, text, buttons)
        // should blend over the scene.
        float uiBrightness = max(ui.r, max(ui.g, ui.b));
        float effectiveAlpha = (uiBrightness < 0.03) ? 0.0 : ui.a;

        if (mode == 1) {
            // ScRGB (FP16 linear space, where 1.0 = 80 nits)
            vec3 sceneScRGB = sceneColor * pc.hdrParams.x;
            vec3 uiScRGB = uiLinear * pc.hdrParams.x;
            vec3 finalColor = mix(sceneScRGB, uiScRGB, effectiveAlpha);
            outColor = vec4(finalColor, 1.0);
        } else if (mode == 2) {
            // HDR10 (BT.2020 + ST 2084 PQ)
            vec3 sceneScRGB = sceneColor * pc.hdrParams.x;
            vec3 uiScRGB = uiLinear * pc.hdrParams.x;
            vec3 finalLinear = mix(sceneScRGB, uiScRGB, effectiveAlpha);
            vec3 color2020 = max(Rec709To2020 * finalLinear, vec3(0.0));
            vec3 L = color2020 * (80.0 / 10000.0);
            vec3 pqColor = LinearToPQ(L);
            outColor = vec4(pqColor, 1.0);
        } else {
            // SDR Mode
            vec3 finalColor = mix(sceneColor, ui.rgb, effectiveAlpha);
            outColor = vec4(clamp(finalColor, 0.0, 1.0), 1.0);
        }
    } else {
        // Outside Viewport: Pure UI panel / Editor area
        vec3 editorBg = vec3(0.08, 0.08, 0.10);
        vec3 finalUI = mix(editorBg, ui.rgb, ui.a);
        vec3 finalUILinear = mix(pow(editorBg, vec3(2.2)), uiLinear, ui.a);

        if (mode == 1) {
            vec3 uiScRGB = finalUILinear * pc.hdrParams.x;
            outColor = vec4(uiScRGB, 1.0);
        } else if (mode == 2) {
            vec3 uiScRGB = finalUILinear * pc.hdrParams.x;
            vec3 color2020 = max(Rec709To2020 * uiScRGB, vec3(0.0));
            vec3 L = color2020 * (80.0 / 10000.0);
            outColor = vec4(LinearToPQ(L), 1.0);
        } else {
            outColor = vec4(clamp(finalUI, 0.0, 1.0), 1.0);
        }
    }
}
