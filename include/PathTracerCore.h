#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <string>
#include <vector>
#include <memory>

#include "Camera.h"
#include "Scene.h"
#include "MaterialManager.h"

namespace neurender {

struct PushConstants {
    glm::vec4 camPos;        // xyz = pos, w = fov (rad)
    glm::vec4 camFront;      // xyz = front, w = aperture
    glm::vec4 camRight;      // xyz = right, w = focusDist
    glm::vec4 camUp;         // xyz = up, w = maxBounces
    glm::uvec4 renderParams; // x = width, y = height, z = frameIndex, w = samplesPerFrame
    glm::vec4 envAndTone;    // xyz = envColor * envIntensity, w = tonemapMode
    glm::vec4 postParams;    // x = gamma, y = exposure, z = randomSeed, w = numLightTriangles
};

class PathTracerCore {
public:
    static void Init();
    static void Shutdown();
    static void RenderFrame();

    // Reset accumulation buffer
    static void ResetAccumulation();

    // Scene and Camera access
    static Scene& GetScene() { return s_Scene; }
    static Camera& GetCamera() { return s_Camera; }

    // Render parameters
    static int GetTargetSPP() { return s_TargetSPP; }
    static void SetTargetSPP(int spp) { s_TargetSPP = spp; }

    static int GetAccumulatedSPP() { return static_cast<int>(s_FrameIndex); }

    static int GetSamplesPerFrame() { return s_SamplesPerFrame; }
    static void SetSamplesPerFrame(int spf) { s_SamplesPerFrame = spf; }

    static int GetMaxBounces() { return s_MaxBounces; }
    static void SetMaxBounces(int b) { s_MaxBounces = b; ResetAccumulation(); }

    static int GetTonemapMode() { return s_TonemapMode; }
    static void SetTonemapMode(int mode) { s_TonemapMode = mode; }

    static float GetExposure() { return s_Exposure; }
    static void SetExposure(float exp) { s_Exposure = exp; }

    static float GetGamma() { return s_Gamma; }
    static void SetGamma(float g) { s_Gamma = g; }

    static glm::vec3 GetEnvColor() { return s_EnvColor; }
    static void SetEnvColor(const glm::vec3& c) { s_EnvColor = c; ResetAccumulation(); }

    static float GetEnvIntensity() { return s_EnvIntensity; }
    static void SetEnvIntensity(float i) { s_EnvIntensity = i; ResetAccumulation(); }

    static uint32_t GetRenderWidth() { return s_RenderWidth; }
    static uint32_t GetRenderHeight() { return s_RenderHeight; }
    static void SetRenderResolution(uint32_t w, uint32_t h);
    static void ResizeRenderResolution(uint32_t w, uint32_t h) { SetRenderResolution(w, h); }
    static void ProcessPendingResize();

    // Bloom Settings
    static bool GetBloomEnabled() { return s_BloomEnabled; }
    static void SetBloomEnabled(bool e) { s_BloomEnabled = e; }
    static float GetBloomThreshold() { return s_BloomThreshold; }
    static void SetBloomThreshold(float t) { s_BloomThreshold = t; }
    static float GetBloomSoftThreshold() { return s_BloomSoftThreshold; }
    static void SetBloomSoftThreshold(float st) { s_BloomSoftThreshold = st; }
    static float GetBloomIntensity() { return s_BloomIntensity; }
    static void SetBloomIntensity(float i) { s_BloomIntensity = i; }
    static float GetBloomRadius() { return s_BloomRadius; }
    static void SetBloomRadius(float r) { s_BloomRadius = r; }

    // Denoiser Settings (Edge-Avoiding À-Trous / Guided Bilateral)
    static bool GetDenoiserEnabled() { return s_DenoiserEnabled; }
    static void SetDenoiserEnabled(bool e) { s_DenoiserEnabled = e; }
    static int GetDenoiserPasses() { return s_DenoiserPasses; }
    static void SetDenoiserPasses(int p) { s_DenoiserPasses = p; }
    static float GetDenoiserColorSigma() { return s_DenoiserColorSigma; }
    static void SetDenoiserColorSigma(float s) { s_DenoiserColorSigma = s; }
    static float GetDenoiserNormalSigma() { return s_DenoiserNormalSigma; }
    static void SetDenoiserNormalSigma(float s) { s_DenoiserNormalSigma = s; }
    static float GetDenoiserDepthSigma() { return s_DenoiserDepthSigma; }
    static void SetDenoiserDepthSigma(float s) { s_DenoiserDepthSigma = s; }

    // Atmosphere & Physical Sky (Nishita 1993)
    static int GetSkyMode() { return s_SkyMode; }
    static void SetSkyMode(int m) { s_SkyMode = m; ResetAccumulation(); }
    static float GetSunElevation() { return s_SunElevation; }
    static void SetSunElevation(float el) { s_SunElevation = el; ResetAccumulation(); }
    static float GetSunAzimuth() { return s_SunAzimuth; }
    static void SetSunAzimuth(float az) { s_SunAzimuth = az; ResetAccumulation(); }
    static float GetSunIntensity() { return s_SunIntensity; }
    static void SetSunIntensity(float i) { s_SunIntensity = i; ResetAccumulation(); }
    static float GetSunAngularSize() { return s_SunAngularSize; }
    static void SetSunAngularSize(float s) { s_SunAngularSize = s; ResetAccumulation(); }
    static float GetRayleighScale() { return s_RayleighScale; }
    static void SetRayleighScale(float r) { s_RayleighScale = r; ResetAccumulation(); }
    static float GetMieTurbidity() { return s_MieTurbidity; }
    static void SetMieTurbidity(float t) { s_MieTurbidity = t; ResetAccumulation(); }
    static glm::vec3 GetGroundAlbedo() { return s_GroundAlbedo; }
    static void SetGroundAlbedo(const glm::vec3& a) { s_GroundAlbedo = a; ResetAccumulation(); }
    static glm::vec3 GetSunDirection();

    // UI Scale
    static float GetUIScale() { return s_UIScale; }
    static void SetUIScale(float scale);

    // ImGui texture handle for viewport
    static ImTextureID GetViewportTextureID() { return s_ViewportTextureID; }

    // Screenshot export
    static void RequestScreenshot(const std::string& filepath, bool captureUI = false);

    // Vulkan getters
    static VkDevice GetDevice() { return s_Device; }
    static VkInstance GetInstance() { return s_Instance; }
    static VkPhysicalDevice GetPhysicalDevice() { return s_PhysicalDevice; }

private:
    // Vulkan Initialization
    static void CreateInstance();
    static void CreateSurface();
    static void PickPhysicalDevice();
    static void CreateLogicalDevice();
    static void CreateSwapchain();
    static void CreateSwapchainImageViews();
    static void CreateRenderPass();
    static void CreateFramebuffers();
    static void CreateCommandPool();
    static void CreateCommandBuffers();
    static void CreateSyncObjects();
    static void CreateDescriptorPool();
    static void InitImGui();

    // Compute Path Tracer resources
    static void CreateComputeResources();
    static void DestroyComputeResources();
    static void CreateComputePipeline();
    static void UpdateSSBOs();
    static void DispatchCompute(VkCommandBuffer cmd);

    // Post-Process & Bloom
    static void CreatePostProcessPipeline();
    static void DestroyPostProcessPipeline();
    static void UpdatePostProcessDescriptorSets();
    static void DispatchPostProcess(VkCommandBuffer cmd);

    struct BloomMip {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
    };
    static constexpr uint32_t BLOOM_MIP_LEVELS = 5;
    static BloomMip s_BloomMips[BLOOM_MIP_LEVELS];
    static VkSampler s_BloomSampler;

    static VkDescriptorSetLayout s_BloomDescriptorSetLayout;
    static VkPipelineLayout s_BloomPipelineLayout;
    static VkPipeline s_BloomThresholdPipeline;
    static VkPipeline s_BloomDownsamplePipeline;
    static VkPipeline s_BloomUpsamplePipeline;
    static std::vector<VkDescriptorSet> s_BloomThresholdDescriptorSets;
    static std::vector<VkDescriptorSet> s_BloomDownsampleDescriptorSets;
    static std::vector<VkDescriptorSet> s_BloomUpsampleDescriptorSets;

    static void CreateBloomResources();
    static void DestroyBloomResources();
    static void CreateBloomPipelines();
    static void DestroyBloomPipelines();
    static void DispatchBloom(VkCommandBuffer cmd);

    // PostProcess Pipeline objects
    static VkDescriptorSetLayout s_PostProcessDescriptorSetLayout;
    static VkPipelineLayout s_PostProcessPipelineLayout;
    static VkPipeline s_PostProcessPipeline;
    static VkDescriptorSet s_PostProcessDescriptorSet;

    // Denoiser Pipeline & Resources
    static VkImage s_NormalDepthImage;
    static VkDeviceMemory s_NormalDepthImageMemory;
    static VkImageView s_NormalDepthImageView;

    static VkImage s_DenoisedImage;
    static VkDeviceMemory s_DenoisedImageMemory;
    static VkImageView s_DenoisedImageView;

    static VkDescriptorSetLayout s_DenoiseDescriptorSetLayout;
    static VkPipelineLayout s_DenoisePipelineLayout;
    static VkPipeline s_DenoisePipeline;
    static VkDescriptorSet s_DenoiseDescriptorSet;

    static void CreateDenoiseResources();
    static void DestroyDenoiseResources();
    static void CreateDenoisePipeline();
    static void DestroyDenoisePipeline();
    static void DispatchDenoise(VkCommandBuffer cmd);

    // Buffer and Image helpers
    static void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags properties, VkBuffer& buffer,
                             VkDeviceMemory& bufferMemory);
    static void CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    static uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    static void CreateImage(uint32_t width, uint32_t height, VkFormat format,
                            VkImageTiling tiling, VkImageUsageFlags usage,
                            VkMemoryPropertyFlags properties, VkImage& image,
                            VkDeviceMemory& imageMemory);
    static void TransitionImageLayout(VkImage image, VkFormat format,
                                      VkImageLayout oldLayout, VkImageLayout newLayout);

    static void ProcessPendingScreenshot();
    static void RecreateSwapchain();

    // Vulkan objects
    static VkInstance s_Instance;
    static VkDebugUtilsMessengerEXT s_DebugMessenger;
    static VkSurfaceKHR s_Surface;
    static VkPhysicalDevice s_PhysicalDevice;
    static VkDevice s_Device;
    static VkQueue s_GraphicsQueue;
    static VkQueue s_PresentQueue;
    static uint32_t s_GraphicsQueueFamily;
    static uint32_t s_PresentQueueFamily;

    static VkSwapchainKHR s_Swapchain;
    static std::vector<VkImage> s_SwapchainImages;
    static std::vector<VkImageView> s_SwapchainImageViews;
    static VkFormat s_SwapchainImageFormat;
    static VkExtent2D s_SwapchainExtent;

    static VkRenderPass s_UIRenderPass;
    static std::vector<VkFramebuffer> s_SwapchainFramebuffers;

    static VkCommandPool s_CommandPool;
    static std::vector<VkCommandBuffer> s_CommandBuffers;
    static std::vector<VkSemaphore> s_ImageAvailableSemaphores;
    static std::vector<VkSemaphore> s_RenderFinishedSemaphores;
    static std::vector<VkFence> s_InFlightFences;
    static uint32_t s_CurrentFrame;

    static VkDescriptorPool s_DescriptorPool;

    // Compute Path Tracer objects
    static uint32_t s_RenderWidth;
    static uint32_t s_RenderHeight;

    static VkImage s_AccumImage;
    static VkDeviceMemory s_AccumImageMemory;
    static VkImageView s_AccumImageView;

    static VkImage s_DisplayImage;
    static VkDeviceMemory s_DisplayImageMemory;
    static VkImageView s_DisplayImageView;
    static VkSampler s_DisplaySampler;
    static ImTextureID s_ViewportTextureID;

    // SSBOs
    static VkBuffer s_BVHBuffer;
    static VkDeviceMemory s_BVHBufferMemory;
    static VkDeviceSize s_BVHBufferSize;

    static VkBuffer s_TriangleBuffer;
    static VkDeviceMemory s_TriangleBufferMemory;
    static VkDeviceSize s_TriangleBufferSize;

    static VkBuffer s_MaterialBuffer;
    static VkDeviceMemory s_MaterialBufferMemory;
    static VkDeviceSize s_MaterialBufferSize;

    static VkBuffer s_LightBuffer;
    static VkDeviceMemory s_LightBufferMemory;
    static VkDeviceSize s_LightBufferSize;

    struct SkyUBO {
        glm::vec4 sunDirAndIntensity; // xyz = sunDir, w = sunIntensity
        glm::vec4 atmosphereParams;  // x = (float)skyMode (0=const, 1=Nishita), y = rayleighScale, z = mieTurbidity, w = sunAngularSize
        glm::vec4 groundAlbedo;      // xyz = groundAlbedo, w = 0
    };

    static VkBuffer s_SkyBuffer;
    static VkDeviceMemory s_SkyBufferMemory;
    static void UpdateSkyUBO();

    static VkDescriptorSetLayout s_ComputeDescriptorSetLayout;
    static VkDescriptorSet s_ComputeDescriptorSet;
    static VkPipelineLayout s_ComputePipelineLayout;
    static VkPipeline s_ComputePipeline;

    // State & Parameters
    static Scene s_Scene;
    static Camera s_Camera;

    static uint32_t s_FrameIndex;
    static int s_TargetSPP;
    static int s_SamplesPerFrame;
    static int s_MaxBounces;
    static int s_TonemapMode;
    static float s_Exposure;
    static float s_Gamma;
    static glm::vec3 s_EnvColor;
    static float s_EnvIntensity;

    static int s_SkyMode;
    static float s_SunElevation;
    static float s_SunAzimuth;
    static float s_SunIntensity;
    static float s_SunAngularSize;
    static float s_RayleighScale;
    static float s_MieTurbidity;
    static glm::vec3 s_GroundAlbedo;

    static bool s_BloomEnabled;
    static float s_BloomThreshold;
    static float s_BloomSoftThreshold;
    static float s_BloomIntensity;
    static float s_BloomRadius;

    static bool s_DenoiserEnabled;
    static int s_DenoiserPasses;
    static float s_DenoiserColorSigma;
    static float s_DenoiserNormalSigma;
    static float s_DenoiserDepthSigma;

    static float s_UIScale;

    static std::string s_PendingScreenshotPath;
    static bool s_PendingScreenshotUI;
    static uint32_t s_LastPresentImageIndex;
    static bool s_NeedResize;
    static uint32_t s_NewWidth;
    static uint32_t s_NewHeight;
};

} // namespace neurender
