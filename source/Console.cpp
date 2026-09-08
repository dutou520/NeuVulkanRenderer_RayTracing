#include "Console.h"
#include "PathTracerCore.h"
#include "Window.h"
#include "neuLog.h"
#include "TextureManager.h"
#include "neuGUI.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>

namespace neurender {

std::vector<Console::CommandEntry> Console::s_Commands;
std::vector<std::string> Console::s_PendingCommands;
std::mutex Console::s_QueueMutex;
std::thread Console::s_InputThread;
bool Console::s_StopRequested = false;
bool Console::s_Initialized = false;

namespace {

std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string token;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ' ' || c == '\t') {
            if (!inQuotes && !token.empty()) {
                tokens.push_back(token);
                token.clear();
            } else if (inQuotes) {
                token += c;
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

bool ParseFloat(const std::string& s, float& out) {
    try {
        size_t pos;
        out = std::stof(s, &pos);
        return pos == s.size();
    } catch (...) { return false; }
}

bool ParseInt(const std::string& s, int& out) {
    try {
        size_t pos;
        out = std::stoi(s, &pos);
        return pos == s.size();
    } catch (...) { return false; }
}

bool ParseVec3(const std::vector<std::string>& args, size_t start, glm::vec3& out) {
    if (args.size() < start + 3) return false;
    float x, y, z;
    if (!ParseFloat(args[start], x) || !ParseFloat(args[start+1], y) || !ParseFloat(args[start+2], z))
        return false;
    out = glm::vec3(x, y, z);
    return true;
}

} // namespace

void Console::Init() {
    if (s_Initialized) return;
    s_Initialized = true;
    s_StopRequested = false;

    RegisterCommand("help", "help", "显示所有命令及用法", [](const auto&){
        Console::Print("=== NeuTracingRender Console Commands ===");
        for (const auto& cmd : s_Commands) {
            std::ostringstream oss;
            oss << "  " << std::left << std::setw(24) << cmd.usage << " — " << cmd.description;
            Console::Print(oss.str());
        }
        return true;
    });

    RegisterCommand("quit", "quit | exit", "退出程序", [](const auto&){
        Window::Close();
        Console::Print("Exit requested.");
        return true;
    });
    RegisterCommand("exit", "exit", "退出程序", [](const auto&){
        Window::Close();
        return true;
    });

    RegisterCommand("screenshot", "screenshot [path.png] [--ui]", "保存当前渲染图像为 PNG 截图（可带 --ui 截取包含GUI的窗口全貌）", [](const auto& args){
        std::string path = "output.png";
        bool captureUI = false;
        for (const auto& arg : args) {
            if (arg == "--ui" || arg == "-ui") {
                captureUI = true;
            } else {
                path = arg;
            }
        }
        PathTracerCore::RequestScreenshot(path, captureUI);
        Console::Print("Screenshot requested (" + std::string(captureUI ? "Full Window UI" : "Render Viewport") + ") -> " + path);
        return true;
    });

    RegisterCommand("status", "status | info", "获取当前路径追踪渲染器状态", [](const auto&){
        auto& cam = PathTracerCore::GetCamera();
        auto& scene = PathTracerCore::GetScene();
        auto& matMgr = MaterialManager::Instance();

        std::ostringstream oss;
        oss << "\n=== PathTracer Status ==="
            << "\nSPP: " << PathTracerCore::GetAccumulatedSPP() << " / " << (PathTracerCore::GetTargetSPP() == 0 ? "Infinite" : std::to_string(PathTracerCore::GetTargetSPP()))
            << "\nResolution: " << PathTracerCore::GetRenderWidth() << "x" << PathTracerCore::GetRenderHeight()
            << "\nBounces: " << PathTracerCore::GetMaxBounces() << ", SPF: " << PathTracerCore::GetSamplesPerFrame()
            << "\nCamera Pos: (" << cam.GetPosition().x << ", " << cam.GetPosition().y << ", " << cam.GetPosition().z << ")"
            << "\nCamera Target: (" << cam.GetTarget().x << ", " << cam.GetTarget().y << ", " << cam.GetTarget().z << ")"
            << "\nCamera FOV: " << cam.GetFov() << " deg, Aperture: " << cam.GetAperture() << ", FocusDist: " << cam.GetFocusDistance()
            << "\nLoaded Model: " << (scene.GetCurrentModelPath().empty() ? "(none)" : scene.GetCurrentModelPath())
            << "\nObjects: " << scene.GetObjects().size() << ", Triangles: " << scene.GetTriangles().size() << ", Materials: " << matMgr.GetCount();
        Console::Print(oss.str());
        return true;
    });
    RegisterCommand("info", "info", "获取当前渲染器状态", [](const auto& a){
        return s_Commands[3].handler(a);
    });

    RegisterCommand("camera", "camera pos <x y z> | lookat <x y z> | fov <deg> | reset", "控制相机视角与参数", [](const auto& args){
        if (args.empty()) {
            Console::Print("Usage: camera pos <x y z> | lookat <x y z> | fov <deg> | reset");
            return false;
        }
        auto& cam = PathTracerCore::GetCamera();
        if (args[0] == "pos") {
            glm::vec3 p;
            if (ParseVec3(args, 1, p)) {
                cam.SetPosition(p);
                Console::Print("Camera position updated.");
                return true;
            }
        } else if (args[0] == "lookat" || args[0] == "target") {
            glm::vec3 t;
            if (ParseVec3(args, 1, t)) {
                cam.SetTarget(t);
                Console::Print("Camera target updated.");
                return true;
            }
        } else if (args[0] == "fov") {
            float f;
            if (args.size() > 1 && ParseFloat(args[1], f)) {
                cam.SetFov(f);
                Console::Print("Camera FOV updated.");
                return true;
            }
        } else if (args[0] == "reset") {
            cam.ResetCornellBoxView();
            Console::Print("Camera reset to Cornell Box view.");
            return true;
        }
        Console::Print("Invalid camera arguments.");
        return false;
    });

    RegisterCommand("cam", "cam pos <x y z> | lookat <x y z> | fov <deg> | reset", "camera 命令别名", [](const auto& args){
        std::string s = "camera";
        for (const auto& a : args) {
            s += " " + a;
        }
        Console::Execute(s);
        return true;
    });

    RegisterCommand("render", "render spp <n> | bounces <n> | spf <n> | reset", "设置渲染参数", [](const auto& args){
        if (args.empty()) {
            Console::Print("Usage: render spp <n> | bounces <n> | spf <n> | reset");
            return false;
        }
        if ((args[0] == "spp" || args[0] == "target") && args.size() > 1) {
            int spp;
            if (ParseInt(args[1], spp)) {
                PathTracerCore::SetTargetSPP(spp);
                Console::Print("Target SPP set to " + std::to_string(spp));
                return true;
            }
        } else if (args[0] == "bounces" && args.size() > 1) {
            int b;
            if (ParseInt(args[1], b)) {
                PathTracerCore::SetMaxBounces(b);
                Console::Print("Max bounces set to " + std::to_string(b));
                return true;
            }
        } else if (args[0] == "spf" && args.size() > 1) {
            int spf;
            if (ParseInt(args[1], spf)) {
                PathTracerCore::SetSamplesPerFrame(spf);
                Console::Print("Samples per frame set to " + std::to_string(spf));
                return true;
            }
        } else if (args[0] == "exposure" && args.size() > 1) {
            float exp;
            if (ParseFloat(args[1], exp)) {
                PathTracerCore::SetExposure(exp);
                Console::Print("Exposure set to " + std::to_string(exp) + " (live updated)");
                return true;
            }
        } else if (args[0] == "gamma" && args.size() > 1) {
            float g;
            if (ParseFloat(args[1], g)) {
                PathTracerCore::SetGamma(g);
                Console::Print("Gamma set to " + std::to_string(g) + " (live updated)");
                return true;
            }
        } else if (args[0] == "tonemap" && args.size() > 1) {
            if (args[1] == "aces") PathTracerCore::SetTonemapMode(0);
            else if (args[1] == "reinhard") PathTracerCore::SetTonemapMode(1);
            else if (args[1] == "linear") PathTracerCore::SetTonemapMode(2);
            else {
                int mode;
                if (ParseInt(args[1], mode)) PathTracerCore::SetTonemapMode(mode);
            }
            Console::Print("Tonemap mode updated (live updated).");
            return true;
        } else if (args[0] == "res" && args.size() > 2) {
            int w, h;
            if (ParseInt(args[1], w) && ParseInt(args[2], h)) {
                PathTracerCore::SetRenderResolution(w, h);
                Console::Print("Resolution changed to " + std::to_string(w) + "x" + std::to_string(h));
                return true;
            }
        } else if (args[0] == "preset" && args.size() > 1) {
            if (args[1] == "720p") PathTracerCore::SetRenderResolution(1280, 720);
            else if (args[1] == "1080p") PathTracerCore::SetRenderResolution(1920, 1080);
            else if (args[1] == "1440p" || args[1] == "2k") PathTracerCore::SetRenderResolution(2560, 1440);
            else if (args[1] == "4k") PathTracerCore::SetRenderResolution(3840, 2160);
            Console::Print("Resolution preset applied.");
            return true;
        } else if (args[0] == "reset") {
            PathTracerCore::ResetAccumulation();
            Console::Print("Accumulation buffer reset.");
            return true;
        }
        Console::Print("Invalid render arguments.");
        return false;
    });

    RegisterCommand("bloom", "bloom on | off | set <threshold|intensity|radius|soft|mode|preserve> <val>", "配置 Bloom 泛光参数", [](const auto& args){
        if (args.empty()) {
            const char* modeNames[] = { "Highlight-Preserving", "Soft Screen", "Smooth Envelope", "Direct Additive" };
            int m = PathTracerCore::GetBloomBlendMode();
            const char* mName = (m >= 0 && m <= 3) ? modeNames[m] : "Unknown";
            Console::Print("Bloom status: " + std::string(PathTracerCore::GetBloomEnabled() ? "ON" : "OFF")
                           + ", Mode=" + std::string(mName)
                           + ", Preserve=" + std::to_string(PathTracerCore::GetBloomHighlightPreserve())
                           + ", Intensity=" + std::to_string(PathTracerCore::GetBloomIntensity())
                           + ", Threshold=" + std::to_string(PathTracerCore::GetBloomThreshold())
                           + ", Radius=" + std::to_string(PathTracerCore::GetBloomRadius()));
            return true;
        }
        if (args[0] == "on" || args[0] == "1" || args[0] == "true") {
            PathTracerCore::SetBloomEnabled(true);
            Console::Print("Bloom enabled.");
            return true;
        } else if (args[0] == "off" || args[0] == "0" || args[0] == "false") {
            PathTracerCore::SetBloomEnabled(false);
            Console::Print("Bloom disabled.");
            return true;
        } else if (args[0] == "set" && args.size() > 2) {
            float val;
            if (ParseFloat(args[2], val)) {
                if (args[1] == "threshold" || args[1] == "thresh") PathTracerCore::SetBloomThreshold(val);
                else if (args[1] == "intensity") PathTracerCore::SetBloomIntensity(val);
                else if (args[1] == "radius") PathTracerCore::SetBloomRadius(val);
                else if (args[1] == "soft") PathTracerCore::SetBloomSoftThreshold(val);
                else if (args[1] == "mode") PathTracerCore::SetBloomBlendMode(std::clamp(static_cast<int>(val), 0, 3));
                else if (args[1] == "preserve") PathTracerCore::SetBloomHighlightPreserve(val);
                Console::Print("Bloom " + args[1] + " set to " + std::to_string(val));
                return true;
            }
        }
        Console::Print("Usage: bloom on | off | set <threshold|intensity|radius|soft|mode|preserve> <val>");
        return false;
    });

    RegisterCommand("denoise", "denoise on | off | set <color|normal|depth> <val>", "配置边缘感知降噪器", [](const auto& args){
        if (args.empty()) {
            Console::Print("Denoiser status: " + std::string(PathTracerCore::GetDenoiserEnabled() ? "ON" : "OFF")
                           + ", ColorSigma=" + std::to_string(PathTracerCore::GetDenoiserColorSigma())
                           + ", NormalSigma=" + std::to_string(PathTracerCore::GetDenoiserNormalSigma())
                           + ", DepthSigma=" + std::to_string(PathTracerCore::GetDenoiserDepthSigma()));
            return true;
        }
        if (args[0] == "on" || args[0] == "1" || args[0] == "true") {
            PathTracerCore::SetDenoiserEnabled(true);
            Console::Print("Denoiser enabled.");
            return true;
        } else if (args[0] == "off" || args[0] == "0" || args[0] == "false") {
            PathTracerCore::SetDenoiserEnabled(false);
            Console::Print("Denoiser disabled.");
            return true;
        } else if (args[0] == "set" && args.size() > 2) {
            float val;
            if (ParseFloat(args[2], val)) {
                if (args[1] == "color" || args[1] == "c") PathTracerCore::SetDenoiserColorSigma(val);
                else if (args[1] == "normal" || args[1] == "n") PathTracerCore::SetDenoiserNormalSigma(val);
                else if (args[1] == "depth" || args[1] == "d") PathTracerCore::SetDenoiserDepthSigma(val);
                Console::Print("Denoiser " + args[1] + " set to " + std::to_string(val));
                return true;
            }
        }
        Console::Print("Usage: denoise on | off | set <color|normal|depth> <val>");
        return false;
    });

    RegisterCommand("ui", "ui scale <float>", "调节 UI 缩放比例", [](const auto& args){
        if (args.size() > 1 && args[0] == "scale") {
            float scale;
            if (ParseFloat(args[1], scale)) {
                PathTracerCore::SetUIScale(scale);
                Console::Print("UI scale set to " + std::to_string(scale));
                return true;
            }
        }
        Console::Print("Usage: ui scale <1.0 | 1.25 | 1.5 | 2.0>");
        return false;
    });

    RegisterCommand("material", "material list | set <name|id> <color|roughness|metallic|ior|emission> <values...>", "查询或修改材质", [](const auto& args){
        auto& matMgr = MaterialManager::Instance();
        if (args.empty() || args[0] == "list") {
            std::ostringstream oss;
            oss << "\n=== Materials List (" << matMgr.GetCount() << ") ===";
            for (int i = 0; i < matMgr.GetCount(); ++i) {
                const auto* m = matMgr.GetMaterial(i);
                oss << "\n[" << i << "] " << std::left << std::setw(20) << m->name
                    << " Type=" << static_cast<int>(m->type)
                    << " Albedo=(" << m->albedo.r << "," << m->albedo.g << "," << m->albedo.b << ")"
                    << " Rough=" << m->roughness << " Metal=" << m->metallic << " IOR=" << m->ior;
                if (m->emissionIntensity > 0.01f) {
                    oss << " Emission=" << m->emissionIntensity << "x(" << m->emission.r << "," << m->emission.g << "," << m->emission.b << ")";
                }
            }
            Console::Print(oss.str());
            return true;
        }
        if (args[0] == "set" && args.size() >= 4) {
            int id = -1;
            if (!ParseInt(args[1], id)) {
                id = matMgr.FindMaterial(args[1]);
            }
            Material* mat = matMgr.GetMaterial(id);
            if (!mat) {
                Console::Print("Material not found: " + args[1]);
                return false;
            }
            std::string prop = args[2];
            if (prop == "color" || prop == "albedo") {
                glm::vec3 c;
                if (ParseVec3(args, 3, c)) {
                    mat->albedo = c;
                    matMgr.SetDirty(true);
                    Console::Print("Updated material albedo.");
                    return true;
                }
            } else if (prop == "roughness") {
                float r;
                if (ParseFloat(args[3], r)) {
                    mat->roughness = r;
                    matMgr.SetDirty(true);
                    Console::Print("Updated material roughness.");
                    return true;
                }
            } else if (prop == "metallic") {
                float m;
                if (ParseFloat(args[3], m)) {
                    mat->metallic = m;
                    matMgr.SetDirty(true);
                    Console::Print("Updated material metallic.");
                    return true;
                }
            } else if (prop == "ior") {
                float ior;
                if (ParseFloat(args[3], ior)) {
                    mat->ior = ior;
                    matMgr.SetDirty(true);
                    Console::Print("Updated material IOR.");
                    return true;
                }
            } else if (prop == "emission") {
                float intensity;
                if (ParseFloat(args[3], intensity)) {
                    mat->emissionIntensity = intensity;
                    matMgr.SetDirty(true);
                    Console::Print("Updated material emission intensity.");
                    return true;
                }
            } else if (prop == "albedo_tex" || prop == "diffuse_tex") {
                mat->albedoTexPath = args[3];
                mat->albedoTexIdx = TextureManager::Instance().LoadTexture(mat->albedoTexPath, true);
                matMgr.SetDirty(true);
                PathTracerCore::ResetAccumulation();
                Console::Print("Assigned albedo texture slot " + std::to_string(mat->albedoTexIdx));
                return true;
            } else if (prop == "roughness_tex") {
                mat->roughnessTexPath = args[3];
                mat->roughnessTexIdx = TextureManager::Instance().LoadTexture(mat->roughnessTexPath);
                if (args.size() > 4) {
                    float s;
                    if (ParseFloat(args[4], s)) mat->roughnessScale = s;
                }
                matMgr.SetDirty(true);
                PathTracerCore::ResetAccumulation();
                Console::Print("Assigned roughness texture slot " + std::to_string(mat->roughnessTexIdx));
                return true;
            } else if (prop == "normal_tex") {
                mat->normalTexPath = args[3];
                mat->normalTexIdx = TextureManager::Instance().LoadTexture(mat->normalTexPath);
                if (args.size() > 4) {
                    float s;
                    if (ParseFloat(args[4], s)) mat->normalScale = s;
                }
                matMgr.SetDirty(true);
                PathTracerCore::ResetAccumulation();
                Console::Print("Assigned normal texture slot " + std::to_string(mat->normalTexIdx));
                return true;
            }
        }
        if (args[0] == "default") {
            matMgr.SetupCornellBoxDefaults();
            Console::Print("Reset materials to Cornell Box defaults.");
            return true;
        }
        Console::Print("Usage: material list | material set <name|id> <color|roughness|metallic|ior|emission|albedo_tex|roughness_tex|normal_tex> <values...>");
        return false;
    });

    RegisterCommand("sky", "sky mode <const|nishita> | sky sun <el> <az> [intensity] | sky <turbidity|rayleigh> <val>", "配置天空与大气模型", [](const auto& args){
        if (args.empty()) {
            Console::Print("Sky mode: " + std::to_string(PathTracerCore::GetSkyMode()) +
                           " Sun: Elevation=" + std::to_string(PathTracerCore::GetSunElevation()) +
                           " Azimuth=" + std::to_string(PathTracerCore::GetSunAzimuth()) +
                           " Intensity=" + std::to_string(PathTracerCore::GetSunIntensity()));
            return true;
        }
        if (args[0] == "mode" && args.size() > 1) {
            if (args[1] == "nishita" || args[1] == "1") {
                PathTracerCore::SetSkyMode(1);
                Console::Print("Sky mode set to Nishita 1993 Physical Atmosphere.");
                return true;
            } else {
                PathTracerCore::SetSkyMode(0);
                Console::Print("Sky mode set to Constant Color.");
                return true;
            }
        }
        if (args[0] == "sun") {
            if (args.size() > 1 && (args[1] == "on" || args[1] == "1" || args[1] == "true" || args[1] == "enable")) {
                PathTracerCore::SetSunEnabled(true);
                Console::Print("Sun enabled.");
                return true;
            } else if (args.size() > 1 && (args[1] == "off" || args[1] == "0" || args[1] == "false" || args[1] == "disable")) {
                PathTracerCore::SetSunEnabled(false);
                Console::Print("Sun disabled.");
                return true;
            } else if (args.size() > 2 && args[1] == "size") {
                float sz;
                if (ParseFloat(args[2], sz)) {
                    PathTracerCore::SetSunAngularSize(sz);
                    Console::Print("Sun angular size set to " + std::to_string(sz));
                    return true;
                }
            } else if (args.size() >= 3) {
                float el, az;
                if (ParseFloat(args[1], el) && ParseFloat(args[2], az)) {
                    PathTracerCore::SetSunElevation(el);
                    PathTracerCore::SetSunAzimuth(az);
                    if (args.size() > 3) {
                        float i;
                        if (ParseFloat(args[3], i)) PathTracerCore::SetSunIntensity(i);
                    }
                    Console::Print("Sun updated: Elevation=" + std::to_string(el) + " Azimuth=" + std::to_string(az));
                    return true;
                }
            }
        }
        if (args[0] == "turbidity" && args.size() > 1) {
            float t;
            if (ParseFloat(args[1], t)) {
                PathTracerCore::SetMieTurbidity(t);
                Console::Print("Mie turbidity set to " + std::to_string(t));
                return true;
            }
        }
        if (args[0] == "rayleigh" && args.size() > 1) {
            float r;
            if (ParseFloat(args[1], r)) {
                PathTracerCore::SetRayleighScale(r);
                Console::Print("Rayleigh scale set to " + std::to_string(r));
                return true;
            }
        }
        Console::Print("Usage: sky mode <const|nishita> | sky sun <el> <az> [intensity] | sky <turbidity|rayleigh> <val>");
        return false;
    });

    RegisterCommand("load", "load model <path>", "加载 3D 模型文件 (支持 .obj, .gltf, .glb)", [](const auto& args){
        if (args.size() < 2 || args[0] != "model") {
            Console::Print("Usage: load model <path.obj|path.gltf|path.glb>");
            return false;
        }
        std::string path = args[1];
        if (PathTracerCore::GetScene().LoadModel(path)) {
            std::string lower = path;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
            if (lower.find("康奈尔") != std::string::npos || lower.find("cornell") != std::string::npos) {
                PathTracerCore::GetCamera().ResetCornellBoxView();
            } else {
                PathTracerCore::GetCamera().FrameBounds(PathTracerCore::GetScene().GetBounds());
            }
            PathTracerCore::ResetAccumulation();
            Console::Print("Successfully loaded model: " + path);
            return true;
        } else {
            Console::Print("Failed to load model: " + path);
            return false;
        }
    });

    RegisterCommand("sleep", "sleep <ms>", "暂停指定毫秒（用于脚本自动化等待）", [](const auto& args){
        if (args.empty()) return false;
        int ms = 0;
        if (ParseInt(args[0], ms) && ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return true;
        }
        return false;
    });

    RegisterCommand("window", "window focus <mat|file|log|view|settings>", "聚焦指定窗口", [](const auto& args){
        if (args.empty()) return false;
        std::string target = (args.size() > 1 && args[0] == "focus") ? args[1] : args[0];
        if (target == "mat" || target == "material") EditorGUI::SetWindowFocus("材质管理器 (Material Manager)");
        else if (target == "file" || target == "browser") EditorGUI::SetWindowFocus("文件浏览器 (File Browser)");
        else if (target == "log" || target == "console") EditorGUI::SetWindowFocus("控制台与日志 (Console Log)");
        else if (target == "view" || target == "viewport") EditorGUI::SetWindowFocus("渲染视口 (Viewport)");
        else if (target == "settings" || target == "render") EditorGUI::SetWindowFocus("渲染设置 (Render Settings)");
        else if (target == "scene" || target == "camera") EditorGUI::SetWindowFocus("场景与相机 (Scene & Camera)");
        return true;
    });

    RegisterCommand("file", "file select <path> | dir <path>", "文件浏览器快速选择", [](const auto& args){
        if (args.size() > 1 && args[0] == "select") {
            EditorGUI::GetFileBrowser().SetSelectedFile(args[1]);
            Console::Print("File selected: " + args[1]);
            return true;
        } else if (args.size() > 1 && args[0] == "dir") {
            EditorGUI::GetFileBrowser().SetCurrentPath(args[1]);
            Console::Print("Directory set to: " + args[1]);
            return true;
        }
        return false;
    });

    s_InputThread = std::thread(InputThreadFunc);
    LOG_I("Console initialized with stdin listener thread.");
}

void Console::Shutdown() {
    s_StopRequested = true;
    if (s_InputThread.joinable()) {
        s_InputThread.detach();
    }
}

void Console::Update() {
    std::string line;
    {
        std::lock_guard<std::mutex> lock(s_QueueMutex);
        if (s_PendingCommands.empty()) return;
        line = s_PendingCommands.front();
    }

    auto tokens = Tokenize(line);
    if (tokens.empty()) {
        std::lock_guard<std::mutex> lock(s_QueueMutex);
        if (!s_PendingCommands.empty()) s_PendingCommands.erase(s_PendingCommands.begin());
        return;
    }

    std::string cmdName = tokens[0];
    std::transform(cmdName.begin(), cmdName.end(), cmdName.begin(), [](unsigned char c){ return std::tolower(c); });

    // Non-blocking wait-spp
    if (cmdName == "wait-spp" && tokens.size() > 1) {
        int target = 0;
        if (ParseInt(tokens[1], target)) {
            if (PathTracerCore::GetAccumulatedSPP() < target) {
                return; // Keep waiting, render thread continues
            }
        }
        {
            std::lock_guard<std::mutex> lock(s_QueueMutex);
            if (!s_PendingCommands.empty()) s_PendingCommands.erase(s_PendingCommands.begin());
        }
        LOG_I("Console: Reached target SPP: {}", tokens[1]);
        return;
    }

    // Pop current command
    {
        std::lock_guard<std::mutex> lock(s_QueueMutex);
        if (!s_PendingCommands.empty()) s_PendingCommands.erase(s_PendingCommands.begin());
    }

    auto it = std::find_if(s_Commands.begin(), s_Commands.end(), [&](const CommandEntry& entry){
        return entry.name == cmdName;
    });

    if (it == s_Commands.end()) {
        LOG_W("Console: Unknown command: '{}' (type 'help' for commands)", tokens[0]);
        return;
    }

    std::vector<std::string> args(tokens.begin() + 1, tokens.end());
    LOG_I("Console >>> {}", line);
    try {
        it->handler(args);
    } catch (const std::exception& e) {
        LOG_E("Console error: {}", e.what());
    }
}

void Console::RegisterCommand(const std::string& name, const std::string& usage,
                              const std::string& description, CommandHandler handler) {
    s_Commands.push_back({ name, usage, description, handler });
}

void Console::Execute(const std::string& commandLine) {
    std::lock_guard<std::mutex> lock(s_QueueMutex);
    s_PendingCommands.push_back(commandLine);
}

void Console::Print(const std::string& message) {
    std::lock_guard<std::mutex> lock(s_QueueMutex);
    printf("%s\n", message.c_str());
    fflush(stdout);
}

void Console::InputThreadFunc() {
    std::string line;
    while (!s_StopRequested && std::getline(std::cin, line)) {
        if (!line.empty()) {
            std::lock_guard<std::mutex> lock(s_QueueMutex);
            s_PendingCommands.push_back(line);
        }
    }
}

} // namespace neurender
