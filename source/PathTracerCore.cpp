#include "PathTracerCore.h"
#include "TextureManager.h"
#include "Window.h"
#include "neuLog.h"
#include <SDL3/SDL_vulkan.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <stdexcept>

namespace neurender {

// VK_KHR_acceleration_structure and VK_KHR_ray_tracing_pipeline function pointers
static PFN_vkCreateAccelerationStructureKHR pfn_vkCreateAccelerationStructureKHR = nullptr;
static PFN_vkDestroyAccelerationStructureKHR pfn_vkDestroyAccelerationStructureKHR = nullptr;
static PFN_vkCmdBuildAccelerationStructuresKHR pfn_vkCmdBuildAccelerationStructuresKHR = nullptr;
static PFN_vkGetAccelerationStructureBuildSizesKHR pfn_vkGetAccelerationStructureBuildSizesKHR = nullptr;
static PFN_vkGetAccelerationStructureDeviceAddressKHR pfn_vkGetAccelerationStructureDeviceAddressKHR = nullptr;
static PFN_vkCreateRayTracingPipelinesKHR pfn_vkCreateRayTracingPipelinesKHR = nullptr;
static PFN_vkGetRayTracingShaderGroupHandlesKHR pfn_vkGetRayTracingShaderGroupHandlesKHR = nullptr;
static PFN_vkCmdTraceRaysKHR pfn_vkCmdTraceRaysKHR = nullptr;
static PFN_vkGetBufferDeviceAddressKHR pfn_vkGetBufferDeviceAddressKHR = nullptr;
static PFN_vkSetHdrMetadataEXT pfn_vkSetHdrMetadataEXT = nullptr;

static std::mt19937 s_HostRng(std::random_device{}());

// ========== Static Member Definitions ==========
VkInstance PathTracerCore::s_Instance = VK_NULL_HANDLE;
VkDebugUtilsMessengerEXT PathTracerCore::s_DebugMessenger = VK_NULL_HANDLE;
VkSurfaceKHR PathTracerCore::s_Surface = VK_NULL_HANDLE;
VkPhysicalDevice PathTracerCore::s_PhysicalDevice = VK_NULL_HANDLE;
VkDevice PathTracerCore::s_Device = VK_NULL_HANDLE;
VkQueue PathTracerCore::s_GraphicsQueue = VK_NULL_HANDLE;
VkQueue PathTracerCore::s_PresentQueue = VK_NULL_HANDLE;
uint32_t PathTracerCore::s_GraphicsQueueFamily = 0;
uint32_t PathTracerCore::s_PresentQueueFamily = 0;

VkSwapchainKHR PathTracerCore::s_Swapchain = VK_NULL_HANDLE;
std::vector<VkImage> PathTracerCore::s_SwapchainImages;
std::vector<VkImageView> PathTracerCore::s_SwapchainImageViews;
VkFormat PathTracerCore::s_SwapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
VkExtent2D PathTracerCore::s_SwapchainExtent = {1600, 900};

VkRenderPass PathTracerCore::s_UIRenderPass = VK_NULL_HANDLE;
VkRenderPass PathTracerCore::s_CompositeRenderPass = VK_NULL_HANDLE;
std::vector<VkFramebuffer> PathTracerCore::s_SwapchainFramebuffers;

HDROutputMode PathTracerCore::s_HDROutputMode = HDROutputMode::Auto;
VkColorSpaceKHR PathTracerCore::s_SwapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
bool PathTracerCore::s_IsHDROutputActive = false;
float PathTracerCore::s_PeakLuminanceNits = 1000.0f;
float PathTracerCore::s_PaperWhiteNits = 200.0f;
float PathTracerCore::s_SoftKneeThreshold = 0.85f;
glm::vec4 PathTracerCore::s_ViewportRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

VkImage PathTracerCore::s_UIOffscreenImage = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_UIOffscreenMemory = VK_NULL_HANDLE;
VkImageView PathTracerCore::s_UIOffscreenView = VK_NULL_HANDLE;
VkFramebuffer PathTracerCore::s_UIFramebuffer = VK_NULL_HANDLE;

VkDescriptorSetLayout PathTracerCore::s_CompositeDescriptorSetLayout = VK_NULL_HANDLE;
VkPipelineLayout PathTracerCore::s_CompositePipelineLayout = VK_NULL_HANDLE;
VkPipeline PathTracerCore::s_CompositePipeline = VK_NULL_HANDLE;
VkDescriptorSet PathTracerCore::s_CompositeDescriptorSet = VK_NULL_HANDLE;

VkCommandPool PathTracerCore::s_CommandPool = VK_NULL_HANDLE;
std::vector<VkCommandBuffer> PathTracerCore::s_CommandBuffers;
std::vector<VkSemaphore> PathTracerCore::s_ImageAvailableSemaphores;
std::vector<VkSemaphore> PathTracerCore::s_RenderFinishedSemaphores;
std::vector<VkFence> PathTracerCore::s_InFlightFences;
uint32_t PathTracerCore::s_CurrentFrame = 0;

VkDescriptorPool PathTracerCore::s_DescriptorPool = VK_NULL_HANDLE;

uint32_t PathTracerCore::s_RenderWidth = 1280;
uint32_t PathTracerCore::s_RenderHeight = 720;

VkImage PathTracerCore::s_AccumImage = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_AccumImageMemory = VK_NULL_HANDLE;
VkImageView PathTracerCore::s_AccumImageView = VK_NULL_HANDLE;

VkImage PathTracerCore::s_DisplayImage = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_DisplayImageMemory = VK_NULL_HANDLE;
VkImageView PathTracerCore::s_DisplayImageView = VK_NULL_HANDLE;
VkSampler PathTracerCore::s_DisplaySampler = VK_NULL_HANDLE;
ImTextureID PathTracerCore::s_ViewportTextureID = (ImTextureID)0;

VkBuffer PathTracerCore::s_BVHBuffer = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_BVHBufferMemory = VK_NULL_HANDLE;
VkDeviceSize PathTracerCore::s_BVHBufferSize = 0;

VkBuffer PathTracerCore::s_TriangleBuffer = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_TriangleBufferMemory = VK_NULL_HANDLE;
VkDeviceSize PathTracerCore::s_TriangleBufferSize = 0;

VkBuffer PathTracerCore::s_MaterialBuffer = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_MaterialBufferMemory = VK_NULL_HANDLE;
VkDeviceSize PathTracerCore::s_MaterialBufferSize = 0;

VkBuffer PathTracerCore::s_LightBuffer = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_LightBufferMemory = VK_NULL_HANDLE;
VkDeviceSize PathTracerCore::s_LightBufferSize = 0;

VkBuffer PathTracerCore::s_SkyBuffer = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_SkyBufferMemory = VK_NULL_HANDLE;

int PathTracerCore::s_SkyMode = 1;            // 0 = const, 1 = Nishita 1993
float PathTracerCore::s_SunElevation = 35.0f; // degrees
float PathTracerCore::s_SunAzimuth = 135.0f;  // degrees
float PathTracerCore::s_SunIntensity = 15.0f;
float PathTracerCore::s_SunAngularSize = 0.0093f; // ~0.53 degrees
float PathTracerCore::s_RayleighScale = 1.0f;
float PathTracerCore::s_MieTurbidity = 1.0f;
glm::vec3 PathTracerCore::s_GroundAlbedo = glm::vec3(0.1f, 0.1f, 0.1f);

glm::vec3 PathTracerCore::GetSunDirection() {
  float elRad = glm::radians(s_SunElevation);
  float azRad = glm::radians(s_SunAzimuth);
  return glm::normalize(glm::vec3(std::cos(elRad) * std::sin(azRad),
                                  std::sin(elRad),
                                  std::cos(elRad) * std::cos(azRad)));
}

VkDescriptorSetLayout PathTracerCore::s_ComputeDescriptorSetLayout =
    VK_NULL_HANDLE;
VkDescriptorSet PathTracerCore::s_ComputeDescriptorSet = VK_NULL_HANDLE;
VkPipelineLayout PathTracerCore::s_ComputePipelineLayout = VK_NULL_HANDLE;
VkPipeline PathTracerCore::s_ComputePipeline = VK_NULL_HANDLE;

// Hardware Ray Tracing static members
bool PathTracerCore::s_HardwareRTSupported = false;
RenderBackend PathTracerCore::s_Backend = RenderBackend::ComputeShader_BVH;

VkPhysicalDeviceRayTracingPipelinePropertiesKHR PathTracerCore::s_RTProps{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
VkPhysicalDeviceAccelerationStructureFeaturesKHR PathTracerCore::s_ASFeatures{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};

VkDescriptorSetLayout PathTracerCore::s_RTDescriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorSet PathTracerCore::s_RTDescriptorSet = VK_NULL_HANDLE;
VkPipelineLayout PathTracerCore::s_RTPipelineLayout = VK_NULL_HANDLE;
VkPipeline PathTracerCore::s_RTPipeline = VK_NULL_HANDLE;

VkBuffer PathTracerCore::s_RTVertexBuffer = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_RTVertexBufferMemory = VK_NULL_HANDLE;

VkBuffer PathTracerCore::s_BLASBuffer = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_BLASBufferMemory = VK_NULL_HANDLE;
VkAccelerationStructureKHR PathTracerCore::s_BLAS = VK_NULL_HANDLE;

VkBuffer PathTracerCore::s_TLASBuffer = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_TLASBufferMemory = VK_NULL_HANDLE;
VkAccelerationStructureKHR PathTracerCore::s_TLAS = VK_NULL_HANDLE;

VkBuffer PathTracerCore::s_InstanceBuffer = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_InstanceBufferMemory = VK_NULL_HANDLE;

VkBuffer PathTracerCore::s_SBTBuffer = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_SBTBufferMemory = VK_NULL_HANDLE;
VkStridedDeviceAddressRegionKHR PathTracerCore::s_RaygenRegion{};
VkStridedDeviceAddressRegionKHR PathTracerCore::s_MissRegion{};
VkStridedDeviceAddressRegionKHR PathTracerCore::s_HitRegion{};
VkStridedDeviceAddressRegionKHR PathTracerCore::s_CallableRegion{};

Scene PathTracerCore::s_Scene;
Camera PathTracerCore::s_Camera;

uint32_t PathTracerCore::s_FrameIndex = 0;
int PathTracerCore::s_TargetSPP = 512;
int PathTracerCore::s_SamplesPerFrame = 1;
int PathTracerCore::s_MaxBounces = 12;
int PathTracerCore::s_TonemapMode = 0; // 0 = ACES, 1 = Reinhard, 2 = Linear
float PathTracerCore::s_Exposure = 1.0f;
float PathTracerCore::s_Gamma = 2.2f;
glm::vec3 PathTracerCore::s_EnvColor = glm::vec3(0.0f);
float PathTracerCore::s_EnvIntensity = 0.0f;

std::string PathTracerCore::s_PendingScreenshotPath = "";
bool PathTracerCore::s_PendingScreenshotUI = false;
uint32_t PathTracerCore::s_LastPresentImageIndex = 0;
bool PathTracerCore::s_NeedResize = false;
uint32_t PathTracerCore::s_NewWidth = 0;
uint32_t PathTracerCore::s_NewHeight = 0;

PathTracerCore::BloomMip
    PathTracerCore::s_BloomMips[PathTracerCore::BLOOM_MIP_LEVELS];
VkSampler PathTracerCore::s_BloomSampler = VK_NULL_HANDLE;
VkDescriptorSetLayout PathTracerCore::s_BloomDescriptorSetLayout =
    VK_NULL_HANDLE;
VkPipelineLayout PathTracerCore::s_BloomPipelineLayout = VK_NULL_HANDLE;
VkPipeline PathTracerCore::s_BloomThresholdPipeline = VK_NULL_HANDLE;
VkPipeline PathTracerCore::s_BloomDownsamplePipeline = VK_NULL_HANDLE;
VkPipeline PathTracerCore::s_BloomUpsamplePipeline = VK_NULL_HANDLE;
std::vector<VkDescriptorSet> PathTracerCore::s_BloomThresholdDescriptorSets;
std::vector<VkDescriptorSet> PathTracerCore::s_BloomDownsampleDescriptorSets;
std::vector<VkDescriptorSet> PathTracerCore::s_BloomUpsampleDescriptorSets;

VkDescriptorSetLayout PathTracerCore::s_PostProcessDescriptorSetLayout =
    VK_NULL_HANDLE;
VkPipelineLayout PathTracerCore::s_PostProcessPipelineLayout = VK_NULL_HANDLE;
VkPipeline PathTracerCore::s_PostProcessPipeline = VK_NULL_HANDLE;
VkDescriptorSet PathTracerCore::s_PostProcessDescriptorSet = VK_NULL_HANDLE;

bool PathTracerCore::s_BloomEnabled = true;
float PathTracerCore::s_BloomThreshold = 1.0f;
float PathTracerCore::s_BloomSoftThreshold = 0.5f;
float PathTracerCore::s_BloomIntensity = 0.1f;
float PathTracerCore::s_BloomRadius = 1.0f;
int PathTracerCore::s_BloomBlendMode = 0;
float PathTracerCore::s_BloomHighlightPreserve = 1.0f;

VkImage PathTracerCore::s_NormalDepthImage = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_NormalDepthImageMemory = VK_NULL_HANDLE;
VkImageView PathTracerCore::s_NormalDepthImageView = VK_NULL_HANDLE;

VkImage PathTracerCore::s_DenoisedImage = VK_NULL_HANDLE;
VkDeviceMemory PathTracerCore::s_DenoisedImageMemory = VK_NULL_HANDLE;
VkImageView PathTracerCore::s_DenoisedImageView = VK_NULL_HANDLE;

VkDescriptorSetLayout PathTracerCore::s_DenoiseDescriptorSetLayout =
    VK_NULL_HANDLE;
VkPipelineLayout PathTracerCore::s_DenoisePipelineLayout = VK_NULL_HANDLE;
VkPipeline PathTracerCore::s_DenoisePipeline = VK_NULL_HANDLE;
VkDescriptorSet PathTracerCore::s_DenoiseDescriptorSet = VK_NULL_HANDLE;

bool PathTracerCore::s_DenoiserEnabled = true;
int PathTracerCore::s_DenoiserPasses = 1;
float PathTracerCore::s_DenoiserColorSigma = 0.1f;
float PathTracerCore::s_DenoiserNormalSigma = 32.0f;
float PathTracerCore::s_DenoiserDepthSigma = 0.05f;

float PathTracerCore::s_UIScale = 1.0f;

void PathTracerCore::SetUIScale(float scale) {
  scale = std::clamp(scale, 0.5f, 3.0f);
  s_UIScale = scale;
  ImGui::GetIO().FontGlobalScale = scale;
}

void PathTracerCore::SetRenderResolution(uint32_t w, uint32_t h) {
  w = std::clamp(w, 64u, 7680u);
  h = std::clamp(h, 64u, 4320u);
  if (w == s_RenderWidth && h == s_RenderHeight)
    return;

  s_NewWidth = w;
  s_NewHeight = h;
  s_NeedResize = true;
}

// ========== Initialization ==========

void PathTracerCore::Init() {
  LOG_I("Initializing Vulkan PathTracerCore...");

  CreateInstance();
  LOG_I("Step: CreateInstance done");
  CreateSurface();
  LOG_I("Step: CreateSurface done");
  PickPhysicalDevice();
  LOG_I("Step: PickPhysicalDevice done");
  CreateLogicalDevice();
  LOG_I("Step: CreateLogicalDevice done");

  HDRManager::Init();
  const auto &hdrInfo = HDRManager::GetCurrentInfo();
  if (hdrInfo.isHDREnabled) {
    s_PeakLuminanceNits = hdrInfo.maxLuminance;
  }

  CreateSwapchain();
  LOG_I("Step: CreateSwapchain done");
  CreateSwapchainImageViews();
  LOG_I("Step: CreateSwapchainImageViews done");
  CreateRenderPass();
  LOG_I("Step: CreateRenderPass done");
  CreateCompositeRenderPass();
  LOG_I("Step: CreateCompositeRenderPass done");
  CreateFramebuffers();
  LOG_I("Step: CreateFramebuffers done");
  CreateCommandPool();
  LOG_I("Step: CreateCommandPool done");
  CreateCommandBuffers();
  LOG_I("Step: CreateCommandBuffers done");
  CreateSyncObjects();
  LOG_I("Step: CreateSyncObjects done");
  CreateDescriptorPool();
  LOG_I("Step: CreateDescriptorPool done");
  CreateUIOffscreenResources();
  LOG_I("Step: CreateUIOffscreenResources done");

  // Initialize ImGui first so ImGui_ImplVulkan_AddTexture is valid
  InitImGui();
  LOG_I("Step: InitImGui done");

  TextureManager::Instance().Init(s_Device, s_PhysicalDevice, s_CommandPool,
                                  s_GraphicsQueue);
  LOG_I("Step: TextureManager Init done");

  CreateDenoisePipeline();
  LOG_I("Step: CreateDenoisePipeline done");

  CreateBloomPipelines();
  LOG_I("Step: CreateBloomPipelines done");

  CreatePostProcessPipeline();
  LOG_I("Step: CreatePostProcessPipeline done");

  CreateCompositePipeline();
  LOG_I("Step: CreateCompositePipeline done");

  CreateComputePipeline();
  LOG_I("Step: CreateComputePipeline done");

  CreateComputeResources();
  LOG_I("Step: CreateComputeResources done");

  if (s_HardwareRTSupported) {
    InitHardwareRT();
    LOG_I("Step: InitHardwareRT done");
  }

  LOG_I("PathTracerCore initialized successfully.");
}

void PathTracerCore::Shutdown() {
  vkDeviceWaitIdle(s_Device);

  TextureManager::Instance().Shutdown();

  if (s_SkyBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(s_Device, s_SkyBuffer, nullptr);
    vkFreeMemory(s_Device, s_SkyBufferMemory, nullptr);
    s_SkyBuffer = VK_NULL_HANDLE;
  }

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  if (s_HardwareRTSupported) {
    ShutdownHardwareRT();
  }

  DestroyComputeResources();

  DestroyCompositePipeline();
  DestroyPostProcessPipeline();
  DestroyBloomPipelines();
  DestroyDenoisePipeline();

  DestroyUIOffscreenResources();

  if (s_ComputePipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(s_Device, s_ComputePipeline, nullptr);
    s_ComputePipeline = VK_NULL_HANDLE;
  }
  if (s_ComputePipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(s_Device, s_ComputePipelineLayout, nullptr);
    s_ComputePipelineLayout = VK_NULL_HANDLE;
  }
  if (s_ComputeDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(s_Device, s_ComputeDescriptorSetLayout,
                                 nullptr);
    s_ComputeDescriptorSetLayout = VK_NULL_HANDLE;
  }

  for (size_t i = 0; i < 2; ++i) {
    vkDestroySemaphore(s_Device, s_ImageAvailableSemaphores[i], nullptr);
    vkDestroySemaphore(s_Device, s_RenderFinishedSemaphores[i], nullptr);
    vkDestroyFence(s_Device, s_InFlightFences[i], nullptr);
  }

  vkDestroyCommandPool(s_Device, s_CommandPool, nullptr);
  vkDestroyDescriptorPool(s_Device, s_DescriptorPool, nullptr);

  for (auto fb : s_SwapchainFramebuffers)
    vkDestroyFramebuffer(s_Device, fb, nullptr);
  vkDestroyRenderPass(s_Device, s_CompositeRenderPass, nullptr);
  vkDestroyRenderPass(s_Device, s_UIRenderPass, nullptr);
  for (auto iv : s_SwapchainImageViews)
    vkDestroyImageView(s_Device, iv, nullptr);
  vkDestroySwapchainKHR(s_Device, s_Swapchain, nullptr);

  vkDestroyDevice(s_Device, nullptr);
  vkDestroySurfaceKHR(s_Instance, s_Surface, nullptr);
  vkDestroyInstance(s_Instance, nullptr);

  LOG_I("PathTracerCore shut down cleanly.");
}

void PathTracerCore::SetBackend(RenderBackend backend) {
  if (backend == RenderBackend::HardwareRTX_KHR && !s_HardwareRTSupported) {
    LOG_W("Cannot switch to Hardware RTX backend: not supported on this device!");
    return;
  }
  if (s_Backend != backend) {
    s_Backend = backend;
    ResetAccumulation();
    LOG_I("Switched render backend to: {}",
          backend == RenderBackend::HardwareRTX_KHR ? "Hardware RTX (VK_KHR_ray_tracing_pipeline)" : "Compute Shader (Software BVH)");
  }
}

void PathTracerCore::ResetAccumulation() { s_FrameIndex = 0; }

// ========== Vulkan Setup Implementations ==========

void PathTracerCore::CreateInstance() {
  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "NeuTracingRender";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "NeuTracingEngine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_3;

  Uint32 sdlExtensionCount = 0;
  const char *const *sdlExtensions =
      SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
  std::vector<const char *> extensions(sdlExtensions,
                                       sdlExtensions + sdlExtensionCount);

  uint32_t availInstExtCount = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &availInstExtCount, nullptr);
  std::vector<VkExtensionProperties> availInstExts(availInstExtCount);
  vkEnumerateInstanceExtensionProperties(nullptr, &availInstExtCount, availInstExts.data());

  for (const auto &e : availInstExts) {
    if (std::strcmp(e.extensionName, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME) == 0) {
      extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
      LOG_I("Enabled Vulkan instance extension: {}", VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
      break;
    }
  }

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  if (vkCreateInstance(&createInfo, nullptr, &s_Instance) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan instance!");
  }
  LOG_I("Vulkan instance created.");
}

void PathTracerCore::CreateSurface() {
  if (!SDL_Vulkan_CreateSurface(Window::GetNativeWindow(), s_Instance, nullptr,
                                &s_Surface)) {
    throw std::runtime_error("Failed to create window surface!");
  }
  LOG_I("Window surface created.");
}

void PathTracerCore::PickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(s_Instance, &deviceCount, nullptr);
  if (deviceCount == 0)
    throw std::runtime_error("No Vulkan GPUs found!");

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(s_Instance, &deviceCount, devices.data());

  for (const auto &device : devices) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      s_PhysicalDevice = device;
      LOG_I("Selected Discrete GPU: {}", props.deviceName);
      break;
    }
  }
  if (s_PhysicalDevice == VK_NULL_HANDLE) {
    s_PhysicalDevice = devices[0];
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(s_PhysicalDevice, &props);
    LOG_I("Selected GPU: {}", props.deviceName);
  }
}

void PathTracerCore::CreateLogicalDevice() {
  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(s_PhysicalDevice, &queueFamilyCount,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(s_PhysicalDevice, &queueFamilyCount,
                                           queueFamilies.data());

  int graphicsFamily = -1;
  int presentFamily = -1;

  for (int i = 0; i < static_cast<int>(queueFamilyCount); ++i) {
    if (queueFamilies[i].queueFlags &
        (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
      graphicsFamily = i;
    }
    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(s_PhysicalDevice, i, s_Surface,
                                         &presentSupport);
    if (presentSupport) {
      presentFamily = i;
    }
    if (graphicsFamily >= 0 && presentFamily >= 0)
      break;
  }

  s_GraphicsQueueFamily = static_cast<uint32_t>(graphicsFamily);
  s_PresentQueueFamily = static_cast<uint32_t>(presentFamily);

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> uniqueQueueFamilies = {s_GraphicsQueueFamily,
                                            s_PresentQueueFamily};

  float queuePriority = 1.0f;
  for (uint32_t queueFam : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo qc{};
    qc.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qc.queueFamilyIndex = queueFam;
    qc.queueCount = 1;
    qc.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(qc);
  }

  VkPhysicalDeviceFeatures deviceFeatures{};
  deviceFeatures.samplerAnisotropy = VK_TRUE;

  // Check physical device extensions to see if Hardware RT is supported
  uint32_t extCount = 0;
  vkEnumerateDeviceExtensionProperties(s_PhysicalDevice, nullptr, &extCount, nullptr);
  std::vector<VkExtensionProperties> availableExts(extCount);
  vkEnumerateDeviceExtensionProperties(s_PhysicalDevice, nullptr, &extCount, availableExts.data());

  auto hasExt = [&](const char* name) {
    for (const auto& e : availableExts) {
      if (std::strcmp(e.extensionName, name) == 0) return true;
    }
    return false;
  };

  std::vector<const char *> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  if (hasExt(VK_EXT_HDR_METADATA_EXTENSION_NAME)) {
    deviceExtensions.push_back(VK_EXT_HDR_METADATA_EXTENSION_NAME);
    LOG_I("Vulkan device extension {} detected and requested.", VK_EXT_HDR_METADATA_EXTENSION_NAME);
  }

  bool rtCapable = hasExt(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                   hasExt(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
                   hasExt(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) &&
                   hasExt(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

  VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
  bdaFeatures.bufferDeviceAddress = VK_TRUE;

  VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  asFeatures.accelerationStructure = VK_TRUE;
  asFeatures.pNext = &bdaFeatures;

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  rtFeatures.rayTracingPipeline = VK_TRUE;
  rtFeatures.pNext = &asFeatures;

  VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
  indexingFeatures.runtimeDescriptorArray = VK_TRUE;
  indexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  indexingFeatures.pNext = &rtFeatures;

  if (rtCapable) {
    deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    if (hasExt(VK_KHR_SPIRV_1_4_EXTENSION_NAME)) {
      deviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
    }
    if (hasExt(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME)) {
      deviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
    }
    LOG_I("Hardware Ray Tracing extensions detected and requested.");
  } else {
    LOG_W("Hardware Ray Tracing extensions NOT supported on this device. Fallback to Compute Shader.");
  }

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.pNext = rtCapable ? &indexingFeatures : nullptr;
  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();
  createInfo.pEnabledFeatures = &deviceFeatures;
  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  if (vkCreateDevice(s_PhysicalDevice, &createInfo, nullptr, &s_Device) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create logical device!");
  }

  vkGetDeviceQueue(s_Device, s_GraphicsQueueFamily, 0, &s_GraphicsQueue);
  vkGetDeviceQueue(s_Device, s_PresentQueueFamily, 0, &s_PresentQueue);

  if (hasExt(VK_EXT_HDR_METADATA_EXTENSION_NAME)) {
    pfn_vkSetHdrMetadataEXT = (PFN_vkSetHdrMetadataEXT)vkGetDeviceProcAddr(s_Device, "vkSetHdrMetadataEXT");
    LOG_I("Loaded vkSetHdrMetadataEXT function pointer.");
  }

  if (rtCapable) {
    // Load KHR Ray Tracing function pointers
    pfn_vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(s_Device, "vkCreateAccelerationStructureKHR");
    pfn_vkDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(s_Device, "vkDestroyAccelerationStructureKHR");
    pfn_vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(s_Device, "vkCmdBuildAccelerationStructuresKHR");
    pfn_vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(s_Device, "vkGetAccelerationStructureBuildSizesKHR");
    pfn_vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(s_Device, "vkGetAccelerationStructureDeviceAddressKHR");
    pfn_vkCreateRayTracingPipelinesKHR = (PFN_vkCreateRayTracingPipelinesKHR)vkGetDeviceProcAddr(s_Device, "vkCreateRayTracingPipelinesKHR");
    pfn_vkGetRayTracingShaderGroupHandlesKHR = (PFN_vkGetRayTracingShaderGroupHandlesKHR)vkGetDeviceProcAddr(s_Device, "vkGetRayTracingShaderGroupHandlesKHR");
    pfn_vkCmdTraceRaysKHR = (PFN_vkCmdTraceRaysKHR)vkGetDeviceProcAddr(s_Device, "vkCmdTraceRaysKHR");
    pfn_vkGetBufferDeviceAddressKHR = (PFN_vkGetBufferDeviceAddressKHR)vkGetDeviceProcAddr(s_Device, "vkGetBufferDeviceAddressKHR");

    s_HardwareRTSupported = (pfn_vkCreateAccelerationStructureKHR &&
                             pfn_vkDestroyAccelerationStructureKHR &&
                             pfn_vkCmdBuildAccelerationStructuresKHR &&
                             pfn_vkGetAccelerationStructureBuildSizesKHR &&
                             pfn_vkGetAccelerationStructureDeviceAddressKHR &&
                             pfn_vkCreateRayTracingPipelinesKHR &&
                             pfn_vkGetRayTracingShaderGroupHandlesKHR &&
                             pfn_vkCmdTraceRaysKHR &&
                             pfn_vkGetBufferDeviceAddressKHR);

    if (s_HardwareRTSupported) {
      // Query RT pipeline properties
      VkPhysicalDeviceProperties2 prop2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
      s_RTProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
      prop2.pNext = &s_RTProps;
      vkGetPhysicalDeviceProperties2(s_PhysicalDevice, &prop2);

      LOG_I("Hardware Ray Tracing initialized successfully! ShaderGroupHandleSize = {}, MaxRecursionDepth = {}",
            s_RTProps.shaderGroupHandleSize, s_RTProps.maxRayRecursionDepth);

      // Default backend to Hardware RTX if available
      s_Backend = RenderBackend::HardwareRTX_KHR;
    } else {
      LOG_W("Failed to load some VK_KHR_ray_tracing function pointers. Falling back to Compute Shader.");
      s_HardwareRTSupported = false;
      s_Backend = RenderBackend::ComputeShader_BVH;
    }
  } else {
    s_HardwareRTSupported = false;
    s_Backend = RenderBackend::ComputeShader_BVH;
  }

  LOG_I("Logical device and queues created.");
}

void PathTracerCore::CreateSwapchain() {
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(s_PhysicalDevice, s_Surface,
                                            &capabilities);

  s_SwapchainExtent = capabilities.currentExtent;
  if (s_SwapchainExtent.width == UINT32_MAX) {
    int w, h;
    SDL_GetWindowSizeInPixels(Window::GetNativeWindow(), &w, &h);
    s_SwapchainExtent.width =
        std::clamp(static_cast<uint32_t>(w), capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width);
    s_SwapchainExtent.height =
        std::clamp(static_cast<uint32_t>(h), capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height);
  }

  uint32_t imageCount = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      imageCount > capabilities.maxImageCount) {
    imageCount = capabilities.maxImageCount;
  }

  // Enumerate supported surface formats
  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(s_PhysicalDevice, s_Surface, &formatCount, nullptr);
  std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(s_PhysicalDevice, s_Surface, &formatCount, surfaceFormats.data());

  auto hasFormat = [&](VkFormat fmt, VkColorSpaceKHR cs) -> bool {
    for (const auto &sf : surfaceFormats) {
      if (sf.format == fmt && sf.colorSpace == cs) return true;
    }
    return false;
  };

  VkSurfaceFormatKHR chosenFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
  bool foundFormat = false;

  if (s_HDROutputMode == HDROutputMode::Auto || s_HDROutputMode == HDROutputMode::Force_ScRGB) {
    if (hasFormat(VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)) {
      chosenFormat = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT};
      foundFormat = true;
      LOG_I("Selected ScRGB HDR Swapchain (VK_FORMAT_R16G16B16A16_SFLOAT, EXTENDED_SRGB_LINEAR_EXT)");
    }
  }

  if (!foundFormat && (s_HDROutputMode == HDROutputMode::Auto || s_HDROutputMode == HDROutputMode::Force_HDR10)) {
    if (hasFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT)) {
      chosenFormat = {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT};
      foundFormat = true;
      LOG_I("Selected HDR10 Swapchain (VK_FORMAT_A2B10G10R10_UNORM_PACK32, HDR10_ST2084_EXT)");
    }
  }

  if (!foundFormat) {
    if (hasFormat(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)) {
      chosenFormat = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    } else if (!surfaceFormats.empty()) {
      chosenFormat = surfaceFormats[0];
    }
    LOG_I("Selected SDR Swapchain (Format: {}, ColorSpace: {})", static_cast<int>(chosenFormat.format), static_cast<int>(chosenFormat.colorSpace));
  }

  s_SwapchainImageFormat = chosenFormat.format;
  s_SwapchainColorSpace = chosenFormat.colorSpace;
  s_IsHDROutputActive = (s_SwapchainColorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);
  s_TonemapMode = s_IsHDROutputActive ? 3 : 0;

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = s_Surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = s_SwapchainImageFormat;
  createInfo.imageColorSpace = s_SwapchainColorSpace;
  createInfo.imageExtent = s_SwapchainExtent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

  uint32_t queueFamilyIndices[] = {s_GraphicsQueueFamily, s_PresentQueueFamily};
  if (s_GraphicsQueueFamily != s_PresentQueueFamily) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // VSync
  createInfo.clipped = VK_TRUE;

  if (vkCreateSwapchainKHR(s_Device, &createInfo, nullptr, &s_Swapchain) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create swapchain!");
  }

  vkGetSwapchainImagesKHR(s_Device, s_Swapchain, &imageCount, nullptr);
  s_SwapchainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(s_Device, s_Swapchain, &imageCount,
                          s_SwapchainImages.data());

  if (pfn_vkSetHdrMetadataEXT && s_IsHDROutputActive) {
    VkHdrMetadataEXT hdrMeta{VK_STRUCTURE_TYPE_HDR_METADATA_EXT};
    hdrMeta.displayPrimaryRed = {0.680f, 0.320f};
    hdrMeta.displayPrimaryGreen = {0.265f, 0.690f};
    hdrMeta.displayPrimaryBlue = {0.150f, 0.060f};
    hdrMeta.whitePoint = {0.3127f, 0.3290f}; // D65
    hdrMeta.maxLuminance = s_PeakLuminanceNits;
    hdrMeta.minLuminance = 0.001f;
    hdrMeta.maxContentLightLevel = s_PeakLuminanceNits;
    hdrMeta.maxFrameAverageLightLevel = s_PaperWhiteNits;
    pfn_vkSetHdrMetadataEXT(s_Device, 1, &s_Swapchain, &hdrMeta);
    LOG_I("Set display HDR metadata: Peak={:.1f} nits, PaperWhite={:.1f} nits", s_PeakLuminanceNits, s_PaperWhiteNits);
  }

  LOG_I("Swapchain created ({} images, {}x{}, format {}, colorspace {}).", imageCount,
        s_SwapchainExtent.width, s_SwapchainExtent.height,
        static_cast<int>(s_SwapchainImageFormat), static_cast<int>(s_SwapchainColorSpace));
}

void PathTracerCore::CreateSwapchainImageViews() {
  s_SwapchainImageViews.resize(s_SwapchainImages.size());
  for (size_t i = 0; i < s_SwapchainImages.size(); ++i) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = s_SwapchainImages[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = s_SwapchainImageFormat;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(s_Device, &createInfo, nullptr,
                          &s_SwapchainImageViews[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create swapchain image views!");
    }
  }
}

void PathTracerCore::CreateRenderPass() {
  // s_UIRenderPass: Renders ImGui to offscreen s_UIOffscreenImage (RGBA8)
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkAttachmentReference colorAttachmentRef{};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &colorAttachment;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  if (vkCreateRenderPass(s_Device, &renderPassInfo, nullptr, &s_UIRenderPass) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create UI render pass!");
  }
}

void PathTracerCore::CreateCompositeRenderPass() {
  // s_CompositeRenderPass: Composites HDR Viewport + SDR UI directly to Swapchain
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = s_SwapchainImageFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef{};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = 1;
  renderPassInfo.pAttachments = &colorAttachment;
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  if (vkCreateRenderPass(s_Device, &renderPassInfo, nullptr, &s_CompositeRenderPass) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create Composite render pass!");
  }
}

void PathTracerCore::CreateFramebuffers() {
  s_SwapchainFramebuffers.resize(s_SwapchainImageViews.size());
  for (size_t i = 0; i < s_SwapchainImageViews.size(); ++i) {
    VkImageView attachments[] = {s_SwapchainImageViews[i]};
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = s_CompositeRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = attachments;
    fbInfo.width = s_SwapchainExtent.width;
    fbInfo.height = s_SwapchainExtent.height;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(s_Device, &fbInfo, nullptr,
                            &s_SwapchainFramebuffers[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create swapchain framebuffer!");
    }
  }
}

void PathTracerCore::CreateUIOffscreenResources() {
  CreateImage(s_SwapchainExtent.width, s_SwapchainExtent.height,
              VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_UIOffscreenImage,
              s_UIOffscreenMemory);

  VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = s_UIOffscreenImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  if (vkCreateImageView(s_Device, &viewInfo, nullptr, &s_UIOffscreenView) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create UI offscreen image view!");
  }

  TransitionImageLayout(s_UIOffscreenImage, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  VkImageView attachments[] = {s_UIOffscreenView};
  VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fbInfo.renderPass = s_UIRenderPass;
  fbInfo.attachmentCount = 1;
  fbInfo.pAttachments = attachments;
  fbInfo.width = s_SwapchainExtent.width;
  fbInfo.height = s_SwapchainExtent.height;
  fbInfo.layers = 1;
  if (vkCreateFramebuffer(s_Device, &fbInfo, nullptr, &s_UIFramebuffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create UI offscreen framebuffer!");
  }

  UpdateCompositeDescriptorSets();
  LOG_I("UI Offscreen resources created ({}x{}).", s_SwapchainExtent.width, s_SwapchainExtent.height);
}

void PathTracerCore::DestroyUIOffscreenResources() {
  if (s_UIFramebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(s_Device, s_UIFramebuffer, nullptr);
    s_UIFramebuffer = VK_NULL_HANDLE;
  }
  if (s_UIOffscreenView != VK_NULL_HANDLE) {
    vkDestroyImageView(s_Device, s_UIOffscreenView, nullptr);
    s_UIOffscreenView = VK_NULL_HANDLE;
  }
  if (s_UIOffscreenImage != VK_NULL_HANDLE) {
    vkDestroyImage(s_Device, s_UIOffscreenImage, nullptr);
    s_UIOffscreenImage = VK_NULL_HANDLE;
  }
  if (s_UIOffscreenMemory != VK_NULL_HANDLE) {
    vkFreeMemory(s_Device, s_UIOffscreenMemory, nullptr);
    s_UIOffscreenMemory = VK_NULL_HANDLE;
  }
}

void PathTracerCore::CreateCommandPool() {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = s_GraphicsQueueFamily;

  if (vkCreateCommandPool(s_Device, &poolInfo, nullptr, &s_CommandPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create command pool!");
  }
}

void PathTracerCore::CreateCommandBuffers() {
  s_CommandBuffers.resize(2);
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = s_CommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 2;

  if (vkAllocateCommandBuffers(s_Device, &allocInfo, s_CommandBuffers.data()) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate command buffers!");
  }
}

void PathTracerCore::CreateSyncObjects() {
  s_ImageAvailableSemaphores.resize(2);
  s_RenderFinishedSemaphores.resize(2);
  s_InFlightFences.resize(2);

  VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < 2; ++i) {
    if (vkCreateSemaphore(s_Device, &semInfo, nullptr,
                          &s_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
        vkCreateSemaphore(s_Device, &semInfo, nullptr,
                          &s_RenderFinishedSemaphores[i]) != VK_SUCCESS ||
        vkCreateFence(s_Device, &fenceInfo, nullptr, &s_InFlightFences[i]) !=
            VK_SUCCESS) {
      throw std::runtime_error("Failed to create synchronization objects!");
    }
  }
}

void PathTracerCore::CreateDescriptorPool() {
  VkDescriptorPoolSize poolSizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 200},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 500},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 200},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 200},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 200},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100},
      {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 10}};

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.maxSets = 1000;
  poolInfo.poolSizeCount =
      static_cast<uint32_t>(sizeof(poolSizes) / sizeof(poolSizes[0]));
  poolInfo.pPoolSizes = poolSizes;

  if (vkCreateDescriptorPool(s_Device, &poolInfo, nullptr, &s_DescriptorPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create descriptor pool!");
  }
}

void PathTracerCore::InitImGui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  ImGui::StyleColorsDark();

  // Load Chinese font with 20px for crisp high-DPI rendering
  const char *fontPath = "resource/fonts/SourceHanSansSC-Regular.otf";
  if (std::filesystem::exists(fontPath)) {
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    io.Fonts->AddFontFromFileTTF(fontPath, 20.0f, &fontConfig,
                                 io.Fonts->GetGlyphRangesChineseFull());
    LOG_I("Loaded Chinese font (20px): {}", fontPath);
  } else {
    LOG_W("Chinese font not found at {}, using default font", fontPath);
  }

  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 6.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 4.0f;
  style.TabRounding = 4.0f;
  style.FramePadding = ImVec2(6, 4);
  style.ItemSpacing = ImVec2(8, 6);

  ImGui_ImplSDL3_InitForVulkan(Window::GetNativeWindow());

  ImGui_ImplVulkan_InitInfo initInfo{};
  initInfo.Instance = s_Instance;
  initInfo.PhysicalDevice = s_PhysicalDevice;
  initInfo.Device = s_Device;
  initInfo.QueueFamily = s_GraphicsQueueFamily;
  initInfo.Queue = s_GraphicsQueue;
  initInfo.DescriptorPool = s_DescriptorPool;
  initInfo.RenderPass = s_UIRenderPass;
  initInfo.Subpass = 0;
  initInfo.MinImageCount = 2;
  initInfo.ImageCount = static_cast<uint32_t>(s_SwapchainImages.size());
  initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ImGui_ImplVulkan_Init(&initInfo);
  LOG_I("ImGui initialized.");
}

// Helper to read SPIR-V files
static std::vector<char> ReadSPV(const std::string &path) {
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("Failed to open shader SPV: " + path);
  }
  size_t fileSize = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(fileSize);
  file.seekg(0);
  file.read(buffer.data(), fileSize);
  return buffer;
}

// Inline image barrier helper
static void RecordImageBarrier(VkCommandBuffer cmd, VkImage image,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                               VkPipelineStageFlags srcStage,
                               VkPipelineStageFlags dstStage) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcAccessMask = srcAccess;
  barrier.dstAccessMask = dstAccess;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);
}

// ========== Compute Path Tracer Resources ==========

void PathTracerCore::CreateComputeResources() {
  // 1. Accumulation Image (RGBA32F) - storage + sampled (for bloom threshold
  // sampling)
  CreateImage(s_RenderWidth, s_RenderHeight, VK_FORMAT_R32G32B32A32_SFLOAT,
              VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_AccumImage,
              s_AccumImageMemory);

  VkImageViewCreateInfo accumViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  accumViewInfo.image = s_AccumImage;
  accumViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  accumViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  accumViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  accumViewInfo.subresourceRange.levelCount = 1;
  accumViewInfo.subresourceRange.layerCount = 1;
  vkCreateImageView(s_Device, &accumViewInfo, nullptr, &s_AccumImageView);

  TransitionImageLayout(s_AccumImage, VK_FORMAT_R32G32B32A32_SFLOAT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

  // 2. Display Image (RGBA16F HDR)
  CreateImage(s_RenderWidth, s_RenderHeight, VK_FORMAT_R16G16B16A16_SFLOAT,
              VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_DisplayImage,
              s_DisplayImageMemory);

  VkImageViewCreateInfo dispViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  dispViewInfo.image = s_DisplayImage;
  dispViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  dispViewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  dispViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  dispViewInfo.subresourceRange.levelCount = 1;
  dispViewInfo.subresourceRange.layerCount = 1;
  vkCreateImageView(s_Device, &dispViewInfo, nullptr, &s_DisplayImageView);

  TransitionImageLayout(s_DisplayImage, VK_FORMAT_R16G16B16A16_SFLOAT,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  // 3. Display Sampler
  VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(s_Device, &samplerInfo, nullptr, &s_DisplaySampler);

  // 4. Register with ImGui
  s_ViewportTextureID = (ImTextureID)(uintptr_t)ImGui_ImplVulkan_AddTexture(
      s_DisplaySampler, s_DisplayImageView,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  // 5. Denoise resources
  CreateDenoiseResources();

  // 6. Bloom & PostProcess resources
  CreateBloomResources();
  UpdatePostProcessDescriptorSets();
  UpdateCompositeDescriptorSets();

  LOG_I("Compute, Denoise and PostProcess resources created ({}x{}).",
        s_RenderWidth, s_RenderHeight);
}

void PathTracerCore::DestroyComputeResources() {
  if (s_ViewportTextureID != 0) {
    ImGui_ImplVulkan_RemoveTexture(
        (VkDescriptorSet)(uintptr_t)s_ViewportTextureID);
    s_ViewportTextureID = 0;
  }

  DestroyBloomResources();
  DestroyDenoiseResources();

  if (s_DisplaySampler != VK_NULL_HANDLE) {
    vkDestroySampler(s_Device, s_DisplaySampler, nullptr);
    s_DisplaySampler = VK_NULL_HANDLE;
  }
  if (s_DisplayImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(s_Device, s_DisplayImageView, nullptr);
    s_DisplayImageView = VK_NULL_HANDLE;
  }
  if (s_DisplayImage != VK_NULL_HANDLE) {
    vkDestroyImage(s_Device, s_DisplayImage, nullptr);
    vkFreeMemory(s_Device, s_DisplayImageMemory, nullptr);
    s_DisplayImage = VK_NULL_HANDLE;
  }
  if (s_AccumImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(s_Device, s_AccumImageView, nullptr);
    s_AccumImageView = VK_NULL_HANDLE;
  }
  if (s_AccumImage != VK_NULL_HANDLE) {
    vkDestroyImage(s_Device, s_AccumImage, nullptr);
    vkFreeMemory(s_Device, s_AccumImageMemory, nullptr);
    s_AccumImage = VK_NULL_HANDLE;
  }
}

void PathTracerCore::CreateComputePipeline() {
  auto buffer = ReadSPV("resource/shaders/compiled/pathtrace.comp.spv");

  VkShaderModuleCreateInfo moduleInfo{
      VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  moduleInfo.codeSize = buffer.size();
  moduleInfo.pCode = reinterpret_cast<const uint32_t *>(buffer.data());
  VkShaderModule computeShaderModule;
  if (vkCreateShaderModule(s_Device, &moduleInfo, nullptr,
                           &computeShaderModule) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create compute shader module!");
  }

  // 7 Bindings for path tracing accumulation:
  // Binding 0: Accum Image (Storage Image RGBA32F)
  // Binding 1: BVH Nodes (Storage Buffer)
  // Binding 2: Triangles (Storage Buffer)
  // Binding 3: Materials (Storage Buffer)
  // Binding 4: Light Triangles (Storage Buffer)
  // Binding 5: Textures (Combined Image Sampler Array)
  // Binding 6: SkyParams (Uniform Buffer)
  // Binding 7: Normal & Depth G-Buffer (Storage Image RGBA16F)
  VkDescriptorSetLayoutBinding bindings[] = {
      {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr},
      {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr},
      {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr},
      {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr},
      {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
       TextureManager::MAX_TEXTURES, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr},
      {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr}};

  VkDescriptorSetLayoutCreateInfo layoutInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = 8;
  layoutInfo.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(s_Device, &layoutInfo, nullptr,
                                  &s_ComputeDescriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create compute descriptor set layout!");
  }

  // Push Constants
  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushRange.offset = 0;
  pushRange.size = sizeof(PushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &s_ComputeDescriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;

  if (vkCreatePipelineLayout(s_Device, &pipelineLayoutInfo, nullptr,
                             &s_ComputePipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create compute pipeline layout!");
  }

  // Pipeline
  VkComputePipelineCreateInfo pipelineInfo{
      VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipelineInfo.stage.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipelineInfo.stage.module = computeShaderModule;
  pipelineInfo.stage.pName = "main";
  pipelineInfo.layout = s_ComputePipelineLayout;

  if (vkCreateComputePipelines(s_Device, VK_NULL_HANDLE, 1, &pipelineInfo,
                               nullptr, &s_ComputePipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create compute pipeline!");
  }

  vkDestroyShaderModule(s_Device, computeShaderModule, nullptr);

  // Allocate Descriptor Set
  VkDescriptorSetAllocateInfo allocInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = s_DescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &s_ComputeDescriptorSetLayout;
  if (vkAllocateDescriptorSets(s_Device, &allocInfo, &s_ComputeDescriptorSet) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate compute descriptor set!");
  }

  // Create Sky UBO buffer
  CreateBuffer(sizeof(SkyUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               s_SkyBuffer, s_SkyBufferMemory);
  UpdateSkyUBO();

  LOG_I("Compute path tracing pipeline and SkyUBO created.");
}

// ========== Denoiser Pipeline & Resources ==========

void PathTracerCore::CreateDenoisePipeline() {
  VkDescriptorSetLayoutBinding bindings[] = {
      {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr}, // u_InputImage
      {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr}, // u_NormalDepthImage
      {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr} // u_OutputImage
  };
  VkDescriptorSetLayoutCreateInfo layoutInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = 3;
  layoutInfo.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(s_Device, &layoutInfo, nullptr,
                                  &s_DenoiseDescriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create denoise descriptor set layout!");
  }

  struct DenoisePushConstants {
    glm::uvec4 resolutionAndStep;
    glm::vec4 filterParams;
  };
  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushRange.offset = 0;
  pushRange.size = sizeof(DenoisePushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &s_DenoiseDescriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;
  if (vkCreatePipelineLayout(s_Device, &pipelineLayoutInfo, nullptr,
                             &s_DenoisePipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create denoise pipeline layout!");
  }

  auto spv = ReadSPV("resource/shaders/compiled/denoise.comp.spv");
  VkShaderModuleCreateInfo modInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  modInfo.codeSize = spv.size();
  modInfo.pCode = reinterpret_cast<const uint32_t *>(spv.data());
  VkShaderModule mod;
  if (vkCreateShaderModule(s_Device, &modInfo, nullptr, &mod) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create denoise shader module!");
  }

  VkComputePipelineCreateInfo pipelineInfo{
      VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  pipelineInfo.stage.sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipelineInfo.stage.module = mod;
  pipelineInfo.stage.pName = "main";
  pipelineInfo.layout = s_DenoisePipelineLayout;

  if (vkCreateComputePipelines(s_Device, VK_NULL_HANDLE, 1, &pipelineInfo,
                               nullptr, &s_DenoisePipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create denoise compute pipeline!");
  }
  vkDestroyShaderModule(s_Device, mod, nullptr);

  // Allocate Descriptor Set
  VkDescriptorSetAllocateInfo allocInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = s_DescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &s_DenoiseDescriptorSetLayout;
  if (vkAllocateDescriptorSets(s_Device, &allocInfo, &s_DenoiseDescriptorSet) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate denoise descriptor set!");
  }

  LOG_I("Denoise compute pipeline created.");
}

void PathTracerCore::DestroyDenoisePipeline() {
  if (s_DenoisePipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(s_Device, s_DenoisePipeline, nullptr);
    s_DenoisePipeline = VK_NULL_HANDLE;
  }
  if (s_DenoisePipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(s_Device, s_DenoisePipelineLayout, nullptr);
    s_DenoisePipelineLayout = VK_NULL_HANDLE;
  }
  if (s_DenoiseDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(s_Device, s_DenoiseDescriptorSetLayout,
                                 nullptr);
    s_DenoiseDescriptorSetLayout = VK_NULL_HANDLE;
  }
}

void PathTracerCore::CreateDenoiseResources() {
  // 1. Normal & Depth G-Buffer (RGBA16F)
  CreateImage(s_RenderWidth, s_RenderHeight, VK_FORMAT_R16G16B16A16_SFLOAT,
              VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_NormalDepthImage,
              s_NormalDepthImageMemory);

  VkImageViewCreateInfo ndViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  ndViewInfo.image = s_NormalDepthImage;
  ndViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  ndViewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  ndViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  ndViewInfo.subresourceRange.levelCount = 1;
  ndViewInfo.subresourceRange.layerCount = 1;
  vkCreateImageView(s_Device, &ndViewInfo, nullptr, &s_NormalDepthImageView);

  TransitionImageLayout(s_NormalDepthImage, VK_FORMAT_R16G16B16A16_SFLOAT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

  // 2. Denoised Image (RGBA32F)
  CreateImage(s_RenderWidth, s_RenderHeight, VK_FORMAT_R32G32B32A32_SFLOAT,
              VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_DenoisedImage,
              s_DenoisedImageMemory);

  VkImageViewCreateInfo denoiseViewInfo{
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  denoiseViewInfo.image = s_DenoisedImage;
  denoiseViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  denoiseViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  denoiseViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  denoiseViewInfo.subresourceRange.levelCount = 1;
  denoiseViewInfo.subresourceRange.layerCount = 1;
  vkCreateImageView(s_Device, &denoiseViewInfo, nullptr, &s_DenoisedImageView);

  TransitionImageLayout(s_DenoisedImage, VK_FORMAT_R32G32B32A32_SFLOAT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

  // 3. Update Denoise Descriptor Set
  VkDescriptorImageInfo inputInfo{VK_NULL_HANDLE, s_AccumImageView,
                                  VK_IMAGE_LAYOUT_GENERAL};
  VkDescriptorImageInfo ndInfo{VK_NULL_HANDLE, s_NormalDepthImageView,
                               VK_IMAGE_LAYOUT_GENERAL};
  VkDescriptorImageInfo outputInfo{VK_NULL_HANDLE, s_DenoisedImageView,
                                   VK_IMAGE_LAYOUT_GENERAL};

  VkWriteDescriptorSet writes[] = {
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_DenoiseDescriptorSet,
       0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &inputInfo, nullptr, nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_DenoiseDescriptorSet,
       1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ndInfo, nullptr, nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_DenoiseDescriptorSet,
       2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &outputInfo, nullptr,
       nullptr}};
  vkUpdateDescriptorSets(s_Device, 3, writes, 0, nullptr);

  LOG_I("Denoise resources created ({}x{}).", s_RenderWidth, s_RenderHeight);
}

void PathTracerCore::DestroyDenoiseResources() {
  if (s_NormalDepthImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(s_Device, s_NormalDepthImageView, nullptr);
    s_NormalDepthImageView = VK_NULL_HANDLE;
  }
  if (s_NormalDepthImage != VK_NULL_HANDLE) {
    vkDestroyImage(s_Device, s_NormalDepthImage, nullptr);
    vkFreeMemory(s_Device, s_NormalDepthImageMemory, nullptr);
    s_NormalDepthImage = VK_NULL_HANDLE;
    s_NormalDepthImageMemory = VK_NULL_HANDLE;
  }
  if (s_DenoisedImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(s_Device, s_DenoisedImageView, nullptr);
    s_DenoisedImageView = VK_NULL_HANDLE;
  }
  if (s_DenoisedImage != VK_NULL_HANDLE) {
    vkDestroyImage(s_Device, s_DenoisedImage, nullptr);
    vkFreeMemory(s_Device, s_DenoisedImageMemory, nullptr);
    s_DenoisedImage = VK_NULL_HANDLE;
    s_DenoisedImageMemory = VK_NULL_HANDLE;
  }
}

void PathTracerCore::DispatchDenoise(VkCommandBuffer cmd) {
  if (s_DenoiserEnabled) {
    // Barrier on s_AccumImage & s_NormalDepthImage (compute write -> compute
    // read)
    RecordImageBarrier(cmd, s_AccumImage, VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
                       VK_ACCESS_SHADER_READ_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    RecordImageBarrier(cmd, s_NormalDepthImage, VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
                       VK_ACCESS_SHADER_READ_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_DenoisePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            s_DenoisePipelineLayout, 0, 1,
                            &s_DenoiseDescriptorSet, 0, nullptr);

    struct {
      glm::uvec4 resolutionAndStep;
      glm::vec4 filterParams;
    } denoisePC;

    denoisePC.resolutionAndStep =
        glm::uvec4(s_RenderWidth, s_RenderHeight, 1, 0);
    denoisePC.filterParams = glm::vec4(
        s_DenoiserColorSigma, s_DenoiserNormalSigma, s_DenoiserDepthSigma,
        1.0f / std::max(1.0f, static_cast<float>(s_FrameIndex)));

    vkCmdPushConstants(cmd, s_DenoisePipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(denoisePC),
                       &denoisePC);
    vkCmdDispatch(cmd, (s_RenderWidth + 15) / 16, (s_RenderHeight + 15) / 16,
                  1);

    RecordImageBarrier(cmd, s_DenoisedImage, VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
                       VK_ACCESS_SHADER_READ_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  } else {
    // Passthrough: copy s_AccumImage -> s_DenoisedImage
    RecordImageBarrier(cmd, s_AccumImage, VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);
    RecordImageBarrier(cmd, s_DenoisedImage, VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageCopy copyRegion{};
    copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.extent = {s_RenderWidth, s_RenderHeight, 1};

    vkCmdCopyImage(cmd, s_AccumImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   s_DenoisedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                   &copyRegion);

    RecordImageBarrier(cmd, s_AccumImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT,
                       VK_ACCESS_SHADER_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    RecordImageBarrier(
        cmd, s_DenoisedImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  }
}

// ========== Bloom Pipelines & Resources ==========

void PathTracerCore::CreateBloomPipelines() {
  // Descriptor set layout: Binding 0: Combined Image Sampler, Binding 1:
  // Storage Image
  VkDescriptorSetLayoutBinding bindings[] = {
      {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
       VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr}};
  VkDescriptorSetLayoutCreateInfo layoutInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = 2;
  layoutInfo.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(s_Device, &layoutInfo, nullptr,
                                  &s_BloomDescriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create bloom descriptor set layout!");
  }

  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushRange.offset = 0;
  pushRange.size = 16; // 4 floats

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &s_BloomDescriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;
  if (vkCreatePipelineLayout(s_Device, &pipelineLayoutInfo, nullptr,
                             &s_BloomPipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create bloom pipeline layout!");
  }

  auto threshCode =
      ReadSPV("resource/shaders/compiled/bloom_threshold.comp.spv");
  auto downCode =
      ReadSPV("resource/shaders/compiled/bloom_downsample.comp.spv");
  auto upCode = ReadSPV("resource/shaders/compiled/bloom_upsample.comp.spv");

  VkShaderModuleCreateInfo modInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};

  modInfo.codeSize = threshCode.size();
  modInfo.pCode = reinterpret_cast<const uint32_t *>(threshCode.data());
  VkShaderModule threshMod;
  vkCreateShaderModule(s_Device, &modInfo, nullptr, &threshMod);

  modInfo.codeSize = downCode.size();
  modInfo.pCode = reinterpret_cast<const uint32_t *>(downCode.data());
  VkShaderModule downMod;
  vkCreateShaderModule(s_Device, &modInfo, nullptr, &downMod);

  modInfo.codeSize = upCode.size();
  modInfo.pCode = reinterpret_cast<const uint32_t *>(upCode.data());
  VkShaderModule upMod;
  vkCreateShaderModule(s_Device, &modInfo, nullptr, &upMod);

  VkComputePipelineCreateInfo compInfo{
      VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  compInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  compInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  compInfo.stage.pName = "main";
  compInfo.layout = s_BloomPipelineLayout;

  compInfo.stage.module = threshMod;
  vkCreateComputePipelines(s_Device, VK_NULL_HANDLE, 1, &compInfo, nullptr,
                           &s_BloomThresholdPipeline);

  compInfo.stage.module = downMod;
  vkCreateComputePipelines(s_Device, VK_NULL_HANDLE, 1, &compInfo, nullptr,
                           &s_BloomDownsamplePipeline);

  compInfo.stage.module = upMod;
  vkCreateComputePipelines(s_Device, VK_NULL_HANDLE, 1, &compInfo, nullptr,
                           &s_BloomUpsamplePipeline);

  vkDestroyShaderModule(s_Device, threshMod, nullptr);
  vkDestroyShaderModule(s_Device, downMod, nullptr);
  vkDestroyShaderModule(s_Device, upMod, nullptr);

  // Allocate Bloom Descriptor Sets once (1 threshold + 4 downsample + 4
  // upsample)
  s_BloomThresholdDescriptorSets.resize(1);
  VkDescriptorSetAllocateInfo allocInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = s_DescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &s_BloomDescriptorSetLayout;
  if (vkAllocateDescriptorSets(s_Device, &allocInfo,
                               &s_BloomThresholdDescriptorSets[0]) !=
      VK_SUCCESS) {
    throw std::runtime_error(
        "Failed to allocate bloom threshold descriptor set!");
  }

  s_BloomDownsampleDescriptorSets.resize(BLOOM_MIP_LEVELS - 1);
  for (uint32_t i = 0; i < BLOOM_MIP_LEVELS - 1; ++i) {
    if (vkAllocateDescriptorSets(s_Device, &allocInfo,
                                 &s_BloomDownsampleDescriptorSets[i]) !=
        VK_SUCCESS) {
      throw std::runtime_error(
          "Failed to allocate bloom downsample descriptor set!");
    }
  }

  s_BloomUpsampleDescriptorSets.resize(BLOOM_MIP_LEVELS - 1);
  for (uint32_t i = 0; i < BLOOM_MIP_LEVELS - 1; ++i) {
    if (vkAllocateDescriptorSets(s_Device, &allocInfo,
                                 &s_BloomUpsampleDescriptorSets[i]) !=
        VK_SUCCESS) {
      throw std::runtime_error(
          "Failed to allocate bloom upsample descriptor set!");
    }
  }

  LOG_I("Bloom compute pipelines and descriptor sets created.");
}

void PathTracerCore::DestroyBloomPipelines() {
  if (s_BloomThresholdPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(s_Device, s_BloomThresholdPipeline, nullptr);
    s_BloomThresholdPipeline = VK_NULL_HANDLE;
  }
  if (s_BloomDownsamplePipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(s_Device, s_BloomDownsamplePipeline, nullptr);
    s_BloomDownsamplePipeline = VK_NULL_HANDLE;
  }
  if (s_BloomUpsamplePipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(s_Device, s_BloomUpsamplePipeline, nullptr);
    s_BloomUpsamplePipeline = VK_NULL_HANDLE;
  }
  if (s_BloomPipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(s_Device, s_BloomPipelineLayout, nullptr);
    s_BloomPipelineLayout = VK_NULL_HANDLE;
  }
  if (s_BloomDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(s_Device, s_BloomDescriptorSetLayout, nullptr);
    s_BloomDescriptorSetLayout = VK_NULL_HANDLE;
  }
}

void PathTracerCore::CreateBloomResources() {
  VkSamplerCreateInfo sInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  sInfo.magFilter = VK_FILTER_LINEAR;
  sInfo.minFilter = VK_FILTER_LINEAR;
  sInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(s_Device, &sInfo, nullptr, &s_BloomSampler);

  uint32_t curW = std::max(1u, s_RenderWidth / 2);
  uint32_t curH = std::max(1u, s_RenderHeight / 2);

  for (uint32_t i = 0; i < BLOOM_MIP_LEVELS; ++i) {
    s_BloomMips[i].width = curW;
    s_BloomMips[i].height = curH;

    CreateImage(curW, curH, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_BloomMips[i].image,
                s_BloomMips[i].memory);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = s_BloomMips[i].image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(s_Device, &viewInfo, nullptr, &s_BloomMips[i].view);

    TransitionImageLayout(s_BloomMips[i].image, VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    curW = std::max(1u, curW / 2);
    curH = std::max(1u, curH / 2);
  }

  // Update Threshold: src = s_DenoisedImageView with s_BloomSampler, dst =
  // s_BloomMips[0].view
  VkDescriptorImageInfo threshSrcInfo{s_BloomSampler, s_DenoisedImageView,
                                      VK_IMAGE_LAYOUT_GENERAL};
  VkDescriptorImageInfo threshDstInfo{VK_NULL_HANDLE, s_BloomMips[0].view,
                                      VK_IMAGE_LAYOUT_GENERAL};
  VkWriteDescriptorSet threshWrites[] = {
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
       s_BloomThresholdDescriptorSets[0], 0, 0, 1,
       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &threshSrcInfo, nullptr,
       nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
       s_BloomThresholdDescriptorSets[0], 1, 0, 1,
       VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &threshDstInfo, nullptr, nullptr}};
  vkUpdateDescriptorSets(s_Device, 2, threshWrites, 0, nullptr);

  // Update Downsample: Mip i -> Mip i+1
  for (uint32_t i = 0; i < BLOOM_MIP_LEVELS - 1; ++i) {
    VkDescriptorImageInfo downSrcInfo{s_BloomSampler, s_BloomMips[i].view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo downDstInfo{VK_NULL_HANDLE, s_BloomMips[i + 1].view,
                                      VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet downWrites[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
         s_BloomDownsampleDescriptorSets[i], 0, 0, 1,
         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &downSrcInfo, nullptr,
         nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
         s_BloomDownsampleDescriptorSets[i], 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &downDstInfo, nullptr, nullptr}};
    vkUpdateDescriptorSets(s_Device, 2, downWrites, 0, nullptr);
  }

  // Update Upsample: Mip i+1 -> Mip i
  for (uint32_t i = 0; i < BLOOM_MIP_LEVELS - 1; ++i) {
    VkDescriptorImageInfo upSrcInfo{s_BloomSampler, s_BloomMips[i + 1].view,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo upDstInfo{VK_NULL_HANDLE, s_BloomMips[i].view,
                                    VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet upWrites[] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
         s_BloomUpsampleDescriptorSets[i], 0, 0, 1,
         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &upSrcInfo, nullptr,
         nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
         s_BloomUpsampleDescriptorSets[i], 1, 0, 1,
         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &upDstInfo, nullptr, nullptr}};
    vkUpdateDescriptorSets(s_Device, 2, upWrites, 0, nullptr);
  }

  LOG_I("Bloom resources created ({} mips).", BLOOM_MIP_LEVELS);
}

void PathTracerCore::DestroyBloomResources() {
  if (s_BloomSampler != VK_NULL_HANDLE) {
    vkDestroySampler(s_Device, s_BloomSampler, nullptr);
    s_BloomSampler = VK_NULL_HANDLE;
  }
  for (uint32_t i = 0; i < BLOOM_MIP_LEVELS; ++i) {
    if (s_BloomMips[i].view != VK_NULL_HANDLE) {
      vkDestroyImageView(s_Device, s_BloomMips[i].view, nullptr);
      s_BloomMips[i].view = VK_NULL_HANDLE;
    }
    if (s_BloomMips[i].image != VK_NULL_HANDLE) {
      vkDestroyImage(s_Device, s_BloomMips[i].image, nullptr);
      vkFreeMemory(s_Device, s_BloomMips[i].memory, nullptr);
      s_BloomMips[i].image = VK_NULL_HANDLE;
      s_BloomMips[i].memory = VK_NULL_HANDLE;
    }
  }
}

void PathTracerCore::DispatchBloom(VkCommandBuffer cmd) {
  // 1. Threshold pass: AccumImage -> BloomMip 0
  RecordImageBarrier(
      cmd, s_BloomMips[0].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
      VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    s_BloomThresholdPipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          s_BloomPipelineLayout, 0, 1,
                          &s_BloomThresholdDescriptorSets[0], 0, nullptr);

  struct {
    float threshold;
    float softThreshold;
    glm::vec2 texelSize;
    float invSPP;
  } threshParams;
  threshParams.threshold = s_BloomThreshold;
  threshParams.softThreshold = s_BloomSoftThreshold;
  threshParams.texelSize = {1.0f / static_cast<float>(s_RenderWidth),
                            1.0f / static_cast<float>(s_RenderHeight)};
  threshParams.invSPP = 1.0f / std::max(1.0f, static_cast<float>(s_FrameIndex));

  vkCmdPushConstants(cmd, s_BloomPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(threshParams), &threshParams);
  vkCmdDispatch(cmd, (s_BloomMips[0].width + 15) / 16,
                (s_BloomMips[0].height + 15) / 16, 1);

  RecordImageBarrier(cmd, s_BloomMips[0].image, VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

  // 2. Downsample chain: Mip i -> Mip i+1
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    s_BloomDownsamplePipeline);

  for (uint32_t i = 0; i < BLOOM_MIP_LEVELS - 1; ++i) {
    RecordImageBarrier(
        cmd, s_BloomMips[i + 1].image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
        VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            s_BloomPipelineLayout, 0, 1,
                            &s_BloomDownsampleDescriptorSets[i], 0, nullptr);

    struct {
      glm::vec2 texelSize;
      float mipLevel;
      float pad;
    } downParams;
    downParams.texelSize = {1.0f / static_cast<float>(s_BloomMips[i].width),
                            1.0f / static_cast<float>(s_BloomMips[i].height)};
    downParams.mipLevel = static_cast<float>(i);
    downParams.pad = 0.0f;

    vkCmdPushConstants(cmd, s_BloomPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(downParams), &downParams);
    vkCmdDispatch(cmd, (s_BloomMips[i + 1].width + 15) / 16,
                  (s_BloomMips[i + 1].height + 15) / 16, 1);

    RecordImageBarrier(cmd, s_BloomMips[i + 1].image, VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  }

  // 3. Upsample chain: Mip i+1 -> Mip i (blend from bottom up)
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                    s_BloomUpsamplePipeline);

  for (int i = static_cast<int>(BLOOM_MIP_LEVELS) - 2; i >= 0; --i) {
    RecordImageBarrier(cmd, s_BloomMips[i].image,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            s_BloomPipelineLayout, 0, 1,
                            &s_BloomUpsampleDescriptorSets[i], 0, nullptr);

    struct {
      glm::vec2 texelSize;
      float radius;
      float pad;
    } upParams;
    upParams.texelSize = {1.0f / static_cast<float>(s_BloomMips[i + 1].width),
                          1.0f / static_cast<float>(s_BloomMips[i + 1].height)};
    upParams.radius = s_BloomRadius;
    upParams.pad = 0.0f;

    vkCmdPushConstants(cmd, s_BloomPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(upParams), &upParams);
    vkCmdDispatch(cmd, (s_BloomMips[i].width + 15) / 16,
                  (s_BloomMips[i].height + 15) / 16, 1);

    RecordImageBarrier(cmd, s_BloomMips[i].image, VK_IMAGE_LAYOUT_GENERAL,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                       VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  }
}

// ========== PostProcess Pipeline ==========

void PathTracerCore::CreatePostProcessPipeline() {
  VkDescriptorSetLayoutBinding bindings[] = {
      {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr}, // accumImage
      {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
       VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, // bloomTexture
      {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
       nullptr} // displayImage
  };
  VkDescriptorSetLayoutCreateInfo layoutInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = 3;
  layoutInfo.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(s_Device, &layoutInfo, nullptr,
                                  &s_PostProcessDescriptorSetLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error(
        "Failed to create postprocess descriptor set layout!");
  }

  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushRange.offset = 0;
  pushRange.size = 64; // 13 x 4 bytes (aligned to 64)

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &s_PostProcessDescriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;
  if (vkCreatePipelineLayout(s_Device, &pipelineLayoutInfo, nullptr,
                             &s_PostProcessPipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create postprocess pipeline layout!");
  }

  auto ppCode = ReadSPV("resource/shaders/compiled/postprocess.comp.spv");
  VkShaderModuleCreateInfo modInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  modInfo.codeSize = ppCode.size();
  modInfo.pCode = reinterpret_cast<const uint32_t *>(ppCode.data());
  VkShaderModule ppMod;
  vkCreateShaderModule(s_Device, &modInfo, nullptr, &ppMod);

  VkComputePipelineCreateInfo compInfo{
      VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  compInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  compInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  compInfo.stage.module = ppMod;
  compInfo.stage.pName = "main";
  compInfo.layout = s_PostProcessPipelineLayout;

  if (vkCreateComputePipelines(s_Device, VK_NULL_HANDLE, 1, &compInfo, nullptr,
                               &s_PostProcessPipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create postprocess compute pipeline!");
  }
  vkDestroyShaderModule(s_Device, ppMod, nullptr);

  // Allocate Descriptor Set
  VkDescriptorSetAllocateInfo allocInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = s_DescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &s_PostProcessDescriptorSetLayout;
  if (vkAllocateDescriptorSets(s_Device, &allocInfo,
                               &s_PostProcessDescriptorSet) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate postprocess descriptor set!");
  }

  LOG_I("PostProcess compute pipeline created.");
}

void PathTracerCore::DestroyPostProcessPipeline() {
  if (s_PostProcessPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(s_Device, s_PostProcessPipeline, nullptr);
    s_PostProcessPipeline = VK_NULL_HANDLE;
  }
  if (s_PostProcessPipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(s_Device, s_PostProcessPipelineLayout, nullptr);
    s_PostProcessPipelineLayout = VK_NULL_HANDLE;
  }
  if (s_PostProcessDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(s_Device, s_PostProcessDescriptorSetLayout,
                                 nullptr);
    s_PostProcessDescriptorSetLayout = VK_NULL_HANDLE;
  }
}

void PathTracerCore::UpdatePostProcessDescriptorSets() {
  VkDescriptorImageInfo accumInfo{VK_NULL_HANDLE, s_DenoisedImageView,
                                  VK_IMAGE_LAYOUT_GENERAL};
  VkDescriptorImageInfo bloomInfo{s_BloomSampler, s_BloomMips[0].view,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo dispInfo{VK_NULL_HANDLE, s_DisplayImageView,
                                 VK_IMAGE_LAYOUT_GENERAL};

  VkWriteDescriptorSet writes[] = {
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
       s_PostProcessDescriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       &accumInfo, nullptr, nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
       s_PostProcessDescriptorSet, 1, 0, 1,
       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &bloomInfo, nullptr, nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
       s_PostProcessDescriptorSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       &dispInfo, nullptr, nullptr}};
  vkUpdateDescriptorSets(s_Device, 3, writes, 0, nullptr);
}

void PathTracerCore::DispatchPostProcess(VkCommandBuffer cmd) {
  // 1. Transition display image to GENERAL for compute write
  RecordImageBarrier(
      cmd, s_DisplayImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
      VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

  // 2. Bind Pipeline & Descriptors
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_PostProcessPipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          s_PostProcessPipelineLayout, 0, 1,
                          &s_PostProcessDescriptorSet, 0, nullptr);

  // 3. Push Constants
  struct {
    float exposure;
    float gamma;
    int tonemapMode;
    int bloomEnabled;
    float bloomIntensity;
    float invSPP;
    int width;
    int height;
    float peakLuminance;
    float paperWhite;
    float softKneeThreshold;
    int bloomBlendMode;
    float bloomHighlightPreserve;
  } ppParams;

  ppParams.exposure = s_Exposure;
  ppParams.gamma = s_Gamma;
  ppParams.tonemapMode = s_TonemapMode;
  ppParams.bloomEnabled = s_BloomEnabled ? 1 : 0;
  ppParams.bloomIntensity = s_BloomIntensity;
  ppParams.invSPP = 1.0f / std::max(1.0f, static_cast<float>(s_FrameIndex));
  ppParams.width = static_cast<int>(s_RenderWidth);
  ppParams.height = static_cast<int>(s_RenderHeight);
  ppParams.peakLuminance = s_PeakLuminanceNits;
  ppParams.paperWhite = s_PaperWhiteNits;
  ppParams.softKneeThreshold = s_SoftKneeThreshold;
  ppParams.bloomBlendMode = s_BloomBlendMode;
  ppParams.bloomHighlightPreserve = s_BloomHighlightPreserve;

  vkCmdPushConstants(cmd, s_PostProcessPipelineLayout,
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ppParams),
                     &ppParams);

  // 4. Dispatch
  vkCmdDispatch(cmd, (s_RenderWidth + 15) / 16, (s_RenderHeight + 15) / 16, 1);

  // 5. Transition display image back to SHADER_READ_ONLY_OPTIMAL for Viewport sampling
  RecordImageBarrier(cmd, s_DisplayImage, VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void PathTracerCore::UpdateSkyUBO() {
  if (s_SkyBuffer == VK_NULL_HANDLE)
    return;
  SkyUBO ubo{};
  ubo.sunDirAndIntensity = glm::vec4(GetSunDirection(), s_SunIntensity);
  ubo.atmosphereParams =
      glm::vec4(static_cast<float>(s_SkyMode), s_RayleighScale, s_MieTurbidity,
                s_SunAngularSize);
  ubo.groundAlbedo = glm::vec4(s_GroundAlbedo, 0.0f);

  void *data;
  vkMapMemory(s_Device, s_SkyBufferMemory, 0, sizeof(SkyUBO), 0, &data);
  memcpy(data, &ubo, sizeof(SkyUBO));
  vkUnmapMemory(s_Device, s_SkyBufferMemory);
}

void PathTracerCore::UpdateSSBOs() {
  // 1. BVH Buffer
  const auto &bvh = s_Scene.GetBVH();
  const auto &bvhNodes = bvh.GetNodes();
  VkDeviceSize bvhSize = sizeof(GPUBVHNode) * bvhNodes.size();
  if (s_BVHBuffer == VK_NULL_HANDLE || s_BVHBufferSize < bvhSize) {
    if (s_BVHBuffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(s_Device, s_BVHBuffer, nullptr);
      vkFreeMemory(s_Device, s_BVHBufferMemory, nullptr);
    }
    CreateBuffer(
        bvhSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_BVHBuffer, s_BVHBufferMemory);
    s_BVHBufferSize = bvhSize;
  }
  if (!bvhNodes.empty()) {
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    CreateBuffer(bvhSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);
    void *data;
    vkMapMemory(s_Device, stagingMem, 0, bvhSize, 0, &data);
    memcpy(data, bvhNodes.data(), bvhSize);
    vkUnmapMemory(s_Device, stagingMem);
    CopyBuffer(staging, s_BVHBuffer, bvhSize);
    vkDestroyBuffer(s_Device, staging, nullptr);
    vkFreeMemory(s_Device, stagingMem, nullptr);
  }

  // 2. Triangle Buffer
  const auto &triangles = bvh.GetTriangles();
  const auto &matMgr = MaterialManager::Instance();
  const auto &gpuMaterials = matMgr.GetGPUMaterials();
  const auto &lightTriangles = s_Scene.GetLightTriangles();

  VkDeviceSize triSize = sizeof(GPUTriangle) * triangles.size();
  if (s_TriangleBuffer == VK_NULL_HANDLE || s_TriangleBufferSize < triSize) {
    if (s_TriangleBuffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(s_Device, s_TriangleBuffer, nullptr);
      vkFreeMemory(s_Device, s_TriangleBufferMemory, nullptr);
    }
    VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (s_HardwareRTSupported) {
      usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }
    CreateBuffer(triSize, usageFlags,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_TriangleBuffer,
                 s_TriangleBufferMemory);
    s_TriangleBufferSize = triSize;
  }
  if (!triangles.empty()) {
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    CreateBuffer(triSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);
    void *data;
    vkMapMemory(s_Device, stagingMem, 0, triSize, 0, &data);
    memcpy(data, triangles.data(), triSize);
    vkUnmapMemory(s_Device, stagingMem);
    CopyBuffer(staging, s_TriangleBuffer, triSize);
    vkDestroyBuffer(s_Device, staging, nullptr);
    vkFreeMemory(s_Device, stagingMem, nullptr);
  }

  // 3. Material Buffer
  VkDeviceSize matSize = sizeof(GPUMaterial) * gpuMaterials.size();
  if (s_MaterialBuffer == VK_NULL_HANDLE || s_MaterialBufferSize < matSize) {
    if (s_MaterialBuffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(s_Device, s_MaterialBuffer, nullptr);
      vkFreeMemory(s_Device, s_MaterialBufferMemory, nullptr);
    }
    CreateBuffer(matSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_MaterialBuffer,
                 s_MaterialBufferMemory);
    s_MaterialBufferSize = matSize;
  }
  if (!gpuMaterials.empty()) {
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    CreateBuffer(matSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);
    void *data;
    vkMapMemory(s_Device, stagingMem, 0, matSize, 0, &data);
    memcpy(data, gpuMaterials.data(), matSize);
    vkUnmapMemory(s_Device, stagingMem);
    CopyBuffer(staging, s_MaterialBuffer, matSize);
    vkDestroyBuffer(s_Device, staging, nullptr);
    vkFreeMemory(s_Device, stagingMem, nullptr);
  }

  // 4. Light Buffer
  VkDeviceSize lightSize = sizeof(int) * lightTriangles.size();
  if (s_LightBuffer == VK_NULL_HANDLE || s_LightBufferSize < lightSize) {
    if (s_LightBuffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(s_Device, s_LightBuffer, nullptr);
      vkFreeMemory(s_Device, s_LightBufferMemory, nullptr);
    }
    CreateBuffer(lightSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_LightBuffer,
                 s_LightBufferMemory);
    s_LightBufferSize = lightSize;
  }
  if (!lightTriangles.empty()) {
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    CreateBuffer(sizeof(int) * lightTriangles.size(),
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);
    void *data;
    vkMapMemory(s_Device, stagingMem, 0, sizeof(int) * lightTriangles.size(), 0,
                &data);
    memcpy(data, lightTriangles.data(), sizeof(int) * lightTriangles.size());
    vkUnmapMemory(s_Device, stagingMem);
    CopyBuffer(staging, s_LightBuffer, sizeof(int) * lightTriangles.size());
    vkDestroyBuffer(s_Device, staging, nullptr);
    vkFreeMemory(s_Device, stagingMem, nullptr);
  }

  // Update Compute Descriptor Sets (7 bindings)
  VkDescriptorImageInfo accumImgInfo{};
  accumImgInfo.imageView = s_AccumImageView;
  accumImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  VkDescriptorBufferInfo bvhBufInfo{s_BVHBuffer, 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo triBufInfo{s_TriangleBuffer, 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo matBufInfo{s_MaterialBuffer, 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo lightBufInfo{s_LightBuffer, 0, VK_WHOLE_SIZE};

  auto texInfos = TextureManager::Instance().GetDescriptorImageInfos();
  VkDescriptorBufferInfo skyBufInfo{s_SkyBuffer, 0, sizeof(SkyUBO)};

  VkDescriptorImageInfo normalDepthImgInfo{};
  normalDepthImgInfo.imageView = s_NormalDepthImageView;
  normalDepthImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  VkWriteDescriptorSet descriptorWrites[] = {
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_ComputeDescriptorSet,
       0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &accumImgInfo, nullptr,
       nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_ComputeDescriptorSet,
       1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bvhBufInfo,
       nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_ComputeDescriptorSet,
       2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &triBufInfo,
       nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_ComputeDescriptorSet,
       3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &matBufInfo,
       nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_ComputeDescriptorSet,
       4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &lightBufInfo,
       nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_ComputeDescriptorSet,
       5, 0, TextureManager::MAX_TEXTURES,
       VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texInfos.data(), nullptr,
       nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_ComputeDescriptorSet,
       6, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &skyBufInfo,
       nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_ComputeDescriptorSet,
       7, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &normalDepthImgInfo, nullptr,
       nullptr}};

  vkUpdateDescriptorSets(s_Device, 8, descriptorWrites, 0, nullptr);
  s_Scene.SetDirty(false);
  const_cast<MaterialManager &>(matMgr).SetDirty(false);
  LOG_I("Updated SSBO buffers, textures, and descriptor sets on GPU.");

  if (s_HardwareRTSupported && !triangles.empty()) {
    BuildAccelerationStructures();
  }
}

void PathTracerCore::DispatchCompute(VkCommandBuffer cmd) {
  UpdateSkyUBO();
  // Bind Pipeline & Descriptors
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_ComputePipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          s_ComputePipelineLayout, 0, 1,
                          &s_ComputeDescriptorSet, 0, nullptr);

  // Push Constants
  PushConstants pc{};
  pc.camPos =
      glm::vec4(s_Camera.GetPosition(), glm::radians(s_Camera.GetFov()));
  pc.camFront = glm::vec4(s_Camera.GetForward(), s_Camera.GetAperture());
  pc.camRight = glm::vec4(s_Camera.GetRight(), s_Camera.GetFocusDistance());
  pc.camUp = glm::vec4(s_Camera.GetUp(), static_cast<float>(s_MaxBounces));
  pc.renderParams = glm::uvec4(s_RenderWidth, s_RenderHeight, s_FrameIndex,
                               static_cast<uint32_t>(s_SamplesPerFrame));
  pc.envAndTone =
      glm::vec4(s_EnvColor * s_EnvIntensity, static_cast<float>(s_TonemapMode));
  uint32_t hostSeed = static_cast<uint32_t>(s_HostRng());
  float hostSeedFloat;
  std::memcpy(&hostSeedFloat, &hostSeed, sizeof(float));
  pc.postParams =
      glm::vec4(s_Gamma, s_Exposure, hostSeedFloat,
                static_cast<float>(s_Scene.GetLightTriangles().size()));

  vkCmdPushConstants(cmd, s_ComputePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                     0, sizeof(PushConstants), &pc);

  // Dispatch
  uint32_t groupX = (s_RenderWidth + 15) / 16;
  uint32_t groupY = (s_RenderHeight + 15) / 16;
  vkCmdDispatch(cmd, groupX, groupY, 1);

  // Barrier on s_AccumImage so subsequent Bloom/PostProcess reads the new
  // samples
  RecordImageBarrier(cmd, s_AccumImage, VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

// ========== Main Frame Loop ==========

void PathTracerCore::ProcessPendingResize() {
  if (!s_NeedResize)
    return;
  vkDeviceWaitIdle(s_Device);
  s_RenderWidth = s_NewWidth;
  s_RenderHeight = s_NewHeight;
  s_NeedResize = false;

  DestroyComputeResources();
  CreateComputeResources();
  UpdateSSBOs();
  ResetAccumulation();
}

void PathTracerCore::RenderFrame() {
  vkWaitForFences(s_Device, 1, &s_InFlightFences[s_CurrentFrame], VK_TRUE,
                  UINT64_MAX);

  uint32_t imageIndex;
  VkResult result = vkAcquireNextImageKHR(
      s_Device, s_Swapchain, UINT64_MAX,
      s_ImageAvailableSemaphores[s_CurrentFrame], VK_NULL_HANDLE, &imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    RecreateSwapchain();
    return;
  }

  vkResetFences(s_Device, 1, &s_InFlightFences[s_CurrentFrame]);

  // Check if Camera moved or Scene / Materials changed
  if (s_Camera.IsDirty()) {
    s_Camera.ClearDirty();
    ResetAccumulation();
  }
  if (MaterialManager::Instance().IsDirty()) {
    UpdateSSBOs();
    ResetAccumulation();
  }
  if (s_Scene.IsDirty()) {
    UpdateSSBOs();
    ResetAccumulation();
  }

  VkCommandBuffer cmd = s_CommandBuffers[s_CurrentFrame];
  vkResetCommandBuffer(cmd, 0);

  VkCommandBufferBeginInfo beginInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(cmd, &beginInfo);

  // Dispatch Path Tracer (Hardware RT or Compute Shader) if target SPP not reached
  if (s_TargetSPP <= 0 || static_cast<int>(s_FrameIndex) < s_TargetSPP) {
    if (s_Backend == RenderBackend::HardwareRTX_KHR && s_HardwareRTSupported && s_TLAS != VK_NULL_HANDLE) {
      DispatchHardwareRT(cmd);
    } else {
      DispatchCompute(cmd);
    }
    s_FrameIndex += s_SamplesPerFrame;
  }

  // Dispatch Denoise pass (filters s_AccumImage -> s_DenoisedImage, or copies
  // if disabled)
  DispatchDenoise(cmd);

  // Dispatch Bloom pass if enabled
  if (s_BloomEnabled) {
    DispatchBloom(cmd);
  }

  // Dispatch PostProcess pass (always executed every frame for real-time live
  // exposure/gamma/tonemapping)
  DispatchPostProcess(cmd);

  // 1. UI RenderPass: Render ImGui into offscreen SDR texture (s_UIOffscreenImage)
  VkRenderPassBeginInfo uiPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  uiPassInfo.renderPass = s_UIRenderPass;
  uiPassInfo.framebuffer = s_UIFramebuffer;
  uiPassInfo.renderArea.offset = {0, 0};
  uiPassInfo.renderArea.extent = s_SwapchainExtent;

  VkClearValue uiClearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
  uiPassInfo.clearValueCount = 1;
  uiPassInfo.pClearValues = &uiClearColor;

  vkCmdBeginRenderPass(cmd, &uiPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
  vkCmdEndRenderPass(cmd);

  // 2. Composite RenderPass: Blend HDR Viewport + SDR UI directly into Swapchain
  VkRenderPassBeginInfo compPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  compPassInfo.renderPass = s_CompositeRenderPass;
  compPassInfo.framebuffer = s_SwapchainFramebuffers[imageIndex];
  compPassInfo.renderArea.offset = {0, 0};
  compPassInfo.renderArea.extent = s_SwapchainExtent;

  VkClearValue compClearColor = {{{0.08f, 0.08f, 0.10f, 1.0f}}};
  compPassInfo.clearValueCount = 1;
  compPassInfo.pClearValues = &compClearColor;

  vkCmdBeginRenderPass(cmd, &compPassInfo, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_CompositePipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          s_CompositePipelineLayout, 0, 1,
                          &s_CompositeDescriptorSet, 0, nullptr);

  struct {
    glm::vec4 viewportRect;
    glm::vec4 hdrParams;
    glm::vec4 displayParams;
  } compPC;

  compPC.viewportRect = s_ViewportRect;
  float modeVal = 0.0f;
  if (s_SwapchainColorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) {
    modeVal = 1.0f; // ScRGB
  } else if (s_SwapchainColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
    modeVal = 2.0f; // HDR10
  }
  compPC.hdrParams = glm::vec4(s_PaperWhiteNits / 80.0f, modeVal,
                               s_PeakLuminanceNits, s_SoftKneeThreshold);
  compPC.displayParams = glm::vec4(static_cast<float>(s_TonemapMode), s_Gamma,
                                   s_Exposure, 0.0f);

  vkCmdPushConstants(cmd, s_CompositePipelineLayout,
                     VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(compPC), &compPC);

  VkViewport vp{};
  vp.x = 0.0f;
  vp.y = 0.0f;
  vp.width = static_cast<float>(s_SwapchainExtent.width);
  vp.height = static_cast<float>(s_SwapchainExtent.height);
  vp.minDepth = 0.0f;
  vp.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &vp);

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = s_SwapchainExtent;
  vkCmdSetScissor(cmd, 0, 1, &scissor);

  vkCmdDraw(cmd, 3, 1, 0, 0);

  vkCmdEndRenderPass(cmd);

  vkEndCommandBuffer(cmd);

  // Submit
  VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  VkSemaphore waitSemaphores[] = {s_ImageAvailableSemaphores[s_CurrentFrame]};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  VkSemaphore signalSemaphores[] = {s_RenderFinishedSemaphores[s_CurrentFrame]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  VkResult submitRes = vkQueueSubmit(s_GraphicsQueue, 1, &submitInfo,
                                     s_InFlightFences[s_CurrentFrame]);
  if (submitRes != VK_SUCCESS) {
    LOG_E("vkQueueSubmit failed with VkResult: {}",
          static_cast<int>(submitRes));
    throw std::runtime_error("Failed to submit draw command buffer!");
  }

  // Present
  VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &s_Swapchain;
  presentInfo.pImageIndices = &imageIndex;

  result = vkQueuePresentKHR(s_PresentQueue, &presentInfo);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    RecreateSwapchain();
  }

  s_LastPresentImageIndex = imageIndex;
  s_CurrentFrame = (s_CurrentFrame + 1) % 2;

  ProcessPendingScreenshot();
}

void PathTracerCore::RecreateSwapchain() {
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(Window::GetNativeWindow(), &w, &h);
  if (w == 0 || h == 0)
    return;

  vkDeviceWaitIdle(s_Device);

  DestroyUIOffscreenResources();

  for (auto fb : s_SwapchainFramebuffers)
    vkDestroyFramebuffer(s_Device, fb, nullptr);
  for (auto iv : s_SwapchainImageViews)
    vkDestroyImageView(s_Device, iv, nullptr);
  vkDestroySwapchainKHR(s_Device, s_Swapchain, nullptr);

  CreateSwapchain();
  CreateSwapchainImageViews();

  if (s_CompositeRenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(s_Device, s_CompositeRenderPass, nullptr);
    s_CompositeRenderPass = VK_NULL_HANDLE;
  }
  CreateCompositeRenderPass();

  DestroyCompositePipeline();
  CreateCompositePipeline();

  CreateFramebuffers();
  CreateUIOffscreenResources();
}

void PathTracerCore::CreateCompositePipeline() {
  VkDescriptorSetLayoutBinding bindings[] = {
      {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}, // HDR Viewport
      {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}  // SDR UI
  };
  VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = 2;
  layoutInfo.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(s_Device, &layoutInfo, nullptr, &s_CompositeDescriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create composite descriptor set layout!");
  }

  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  pushRange.offset = 0;
  pushRange.size = 48; // 3 x vec4

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &s_CompositeDescriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;
  if (vkCreatePipelineLayout(s_Device, &pipelineLayoutInfo, nullptr, &s_CompositePipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create composite pipeline layout!");
  }

  auto vertCode = ReadSPV("resource/shaders/compiled/composite.vert.spv");
  auto fragCode = ReadSPV("resource/shaders/compiled/composite.frag.spv");

  VkShaderModule vertModule, fragModule;
  VkShaderModuleCreateInfo vertModInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  vertModInfo.codeSize = vertCode.size();
  vertModInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
  vkCreateShaderModule(s_Device, &vertModInfo, nullptr, &vertModule);

  VkShaderModuleCreateInfo fragModInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  fragModInfo.codeSize = fragCode.size();
  fragModInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
  vkCreateShaderModule(s_Device, &fragModInfo, nullptr, &fragModule);

  VkPipelineShaderStageCreateInfo shaderStages[2]{};
  shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  shaderStages[0].module = vertModule;
  shaderStages[0].pName = "main";

  shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  shaderStages[1].module = fragModule;
  shaderStages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_NONE;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

  VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipelineInfo.stageCount = 2;
  pipelineInfo.pStages = shaderStages;
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = s_CompositePipelineLayout;
  pipelineInfo.renderPass = s_CompositeRenderPass;
  pipelineInfo.subpass = 0;

  if (vkCreateGraphicsPipelines(s_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &s_CompositePipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create composite graphics pipeline!");
  }

  vkDestroyShaderModule(s_Device, fragModule, nullptr);
  vkDestroyShaderModule(s_Device, vertModule, nullptr);

  // Allocate descriptor set
  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = s_DescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &s_CompositeDescriptorSetLayout;
  if (vkAllocateDescriptorSets(s_Device, &allocInfo, &s_CompositeDescriptorSet) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate composite descriptor set!");
  }

  UpdateCompositeDescriptorSets();
  LOG_I("Composite graphics pipeline created.");
}

void PathTracerCore::DestroyCompositePipeline() {
  if (s_CompositePipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(s_Device, s_CompositePipeline, nullptr);
    s_CompositePipeline = VK_NULL_HANDLE;
  }
  if (s_CompositePipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(s_Device, s_CompositePipelineLayout, nullptr);
    s_CompositePipelineLayout = VK_NULL_HANDLE;
  }
  if (s_CompositeDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(s_Device, s_CompositeDescriptorSetLayout, nullptr);
    s_CompositeDescriptorSetLayout = VK_NULL_HANDLE;
  }
}

void PathTracerCore::UpdateCompositeDescriptorSets() {
  if (s_CompositeDescriptorSet == VK_NULL_HANDLE || s_DisplayImageView == VK_NULL_HANDLE ||
      s_UIOffscreenView == VK_NULL_HANDLE || s_DisplaySampler == VK_NULL_HANDLE) {
    return;
  }
  VkDescriptorImageInfo hdrInfo{s_DisplaySampler, s_DisplayImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkDescriptorImageInfo uiInfo{s_DisplaySampler, s_UIOffscreenView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

  VkWriteDescriptorSet writes[2]{};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = s_CompositeDescriptorSet;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[0].pImageInfo = &hdrInfo;

  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet = s_CompositeDescriptorSet;
  writes[1].dstBinding = 1;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[1].pImageInfo = &uiInfo;

  vkUpdateDescriptorSets(s_Device, 2, writes, 0, nullptr);
}

void PathTracerCore::SetHDROutputMode(HDROutputMode mode) {
  if (s_HDROutputMode != mode) {
    s_HDROutputMode = mode;
    RecreateSwapchain();
  }
}

void PathTracerCore::SetViewportRect(float minX, float minY, float maxX,
                                     float maxY, float winW, float winH) {
  if (winW > 0.0f && winH > 0.0f) {
    s_ViewportRect = glm::vec4(
        std::clamp(minX / winW, 0.0f, 1.0f),
        std::clamp(minY / winH, 0.0f, 1.0f),
        std::clamp(maxX / winW, 0.0f, 1.0f),
        std::clamp(maxY / winH, 0.0f, 1.0f));
  }
}

// ========== Screenshot Capture ==========

void PathTracerCore::RequestScreenshot(const std::string &filepath,
                                       bool captureUI) {
  s_PendingScreenshotPath = filepath;
  s_PendingScreenshotUI = captureUI;
}

void PathTracerCore::ProcessPendingScreenshot() {
  if (s_PendingScreenshotPath.empty())
    return;

  vkDeviceWaitIdle(s_Device);

  std::string path = s_PendingScreenshotPath;
  bool captureUI = s_PendingScreenshotUI;
  s_PendingScreenshotPath.clear();
  s_PendingScreenshotUI = false;

  uint32_t width = captureUI ? s_SwapchainExtent.width : s_RenderWidth;
  uint32_t height = captureUI ? s_SwapchainExtent.height : s_RenderHeight;
  VkImage srcImage =
      captureUI ? s_SwapchainImages[s_LastPresentImageIndex] : s_DisplayImage;

  bool isFloat16 = (!captureUI) || (s_SwapchainImageFormat == VK_FORMAT_R16G16B16A16_SFLOAT);
  VkDeviceSize bytesPerPixel = isFloat16 ? 8 : 4;
  VkDeviceSize imageSize = width * height * bytesPerPixel;
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;

  CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  VkCommandBuffer cmd;
  VkCommandBufferAllocateInfo allocInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocInfo.commandPool = s_CommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  vkAllocateCommandBuffers(s_Device, &allocInfo, &cmd);

  VkCommandBufferBeginInfo beginInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  vkBeginCommandBuffer(cmd, &beginInfo);

  VkImageLayout oldLayout = captureUI
                                ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.srcAccessMask =
      captureUI ? VK_ACCESS_MEMORY_READ_BIT : VK_ACCESS_SHADER_READ_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barrier.image = srcImage;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyImageToBuffer(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         stagingBuffer, 1, &region);

  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.newLayout = oldLayout;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barrier.dstAccessMask =
      captureUI ? VK_ACCESS_MEMORY_READ_BIT : VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  vkEndCommandBuffer(cmd);

  VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  vkQueueSubmit(s_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(s_GraphicsQueue);

  vkFreeCommandBuffers(s_Device, s_CommandPool, 1, &cmd);

  void *mappedData;
  vkMapMemory(s_Device, stagingBufferMemory, 0, imageSize, 0, &mappedData);

  stbi_flip_vertically_on_write(0);
  int success = 0;

  bool isHDRFile = (path.size() >= 4 && path.substr(path.size() - 4) == ".hdr");

  if (isHDRFile) {
    std::vector<float> floatRGB(width * height * 3);
    if (isFloat16) {
      const uint16_t *halfPixels = static_cast<const uint16_t *>(mappedData);
      for (size_t i = 0; i < width * height; ++i) {
        glm::vec2 rg = glm::unpackHalf2x16(*reinterpret_cast<const uint32_t *>(&halfPixels[i * 4]));
        glm::vec2 ba = glm::unpackHalf2x16(*reinterpret_cast<const uint32_t *>(&halfPixels[i * 4 + 2]));
        floatRGB[i * 3 + 0] = rg.x;
        floatRGB[i * 3 + 1] = rg.y;
        floatRGB[i * 3 + 2] = ba.x;
      }
    } else {
      const uint8_t *pixels = static_cast<const uint8_t *>(mappedData);
      for (size_t i = 0; i < width * height; ++i) {
        floatRGB[i * 3 + 0] = pixels[i * 4 + 0] / 255.0f;
        floatRGB[i * 3 + 1] = pixels[i * 4 + 1] / 255.0f;
        floatRGB[i * 3 + 2] = pixels[i * 4 + 2] / 255.0f;
      }
    }
    success = stbi_write_hdr(path.c_str(), static_cast<int>(width), static_cast<int>(height), 3, floatRGB.data());
  } else {
    std::vector<uint8_t> pngPixels(width * height * 4);
    if (isFloat16) {
      const uint16_t *halfPixels = static_cast<const uint16_t *>(mappedData);
      for (size_t i = 0; i < width * height; ++i) {
        glm::vec2 rg = glm::unpackHalf2x16(*reinterpret_cast<const uint32_t *>(&halfPixels[i * 4]));
        glm::vec2 ba = glm::unpackHalf2x16(*reinterpret_cast<const uint32_t *>(&halfPixels[i * 4 + 2]));
        float r = rg.x;
        float g = rg.y;
        float b = ba.x;
        if (s_IsHDROutputActive && captureUI) {
          float scale = 80.0f / std::max(s_PaperWhiteNits, 1.0f);
          r = std::pow(std::clamp(r * scale, 0.0f, 1.0f), 1.0f / 2.2f);
          g = std::pow(std::clamp(g * scale, 0.0f, 1.0f), 1.0f / 2.2f);
          b = std::pow(std::clamp(b * scale, 0.0f, 1.0f), 1.0f / 2.2f);
        } else if (s_IsHDROutputActive && !captureUI) {
          r = std::pow(std::clamp(r / (r + 1.0f), 0.0f, 1.0f), 1.0f / 2.2f);
          g = std::pow(std::clamp(g / (g + 1.0f), 0.0f, 1.0f), 1.0f / 2.2f);
          b = std::pow(std::clamp(b / (b + 1.0f), 0.0f, 1.0f), 1.0f / 2.2f);
        } else {
          r = std::clamp(r, 0.0f, 1.0f);
          g = std::clamp(g, 0.0f, 1.0f);
          b = std::clamp(b, 0.0f, 1.0f);
        }
        pngPixels[i * 4 + 0] = static_cast<uint8_t>(r * 255.0f);
        pngPixels[i * 4 + 1] = static_cast<uint8_t>(g * 255.0f);
        pngPixels[i * 4 + 2] = static_cast<uint8_t>(b * 255.0f);
        pngPixels[i * 4 + 3] = 255;
      }
    } else {
      const uint8_t *pixels = static_cast<const uint8_t *>(mappedData);
      for (size_t i = 0; i < width * height; ++i) {
        if (captureUI) {
          pngPixels[i * 4 + 0] = pixels[i * 4 + 2];
          pngPixels[i * 4 + 1] = pixels[i * 4 + 1];
          pngPixels[i * 4 + 2] = pixels[i * 4 + 0];
        } else {
          pngPixels[i * 4 + 0] = pixels[i * 4 + 0];
          pngPixels[i * 4 + 1] = pixels[i * 4 + 1];
          pngPixels[i * 4 + 2] = pixels[i * 4 + 2];
        }
        pngPixels[i * 4 + 3] = 255;
      }
    }
    success = stbi_write_png(path.c_str(), static_cast<int>(width),
                             static_cast<int>(height), 4, pngPixels.data(),
                             static_cast<int>(width * 4));
  }

  vkUnmapMemory(s_Device, stagingBufferMemory);

  vkDestroyBuffer(s_Device, stagingBuffer, nullptr);
  vkFreeMemory(s_Device, stagingBufferMemory, nullptr);

  if (success) {
    LOG_I("Screenshot successfully saved to: {}", path);
  } else {
    LOG_E("Failed to write screenshot to: {}", path);
  }
}

// ========== Buffer & Image Helpers ==========

void PathTracerCore::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties,
                                  VkBuffer &buffer,
                                  VkDeviceMemory &bufferMemory) {
  VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(s_Device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(s_Device, buffer, &memRequirements);

  VkMemoryAllocateFlagsInfo flagsInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
  if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
  }

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
    allocInfo.pNext = &flagsInfo;
  }
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      FindMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(s_Device, &allocInfo, nullptr, &bufferMemory) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate buffer memory!");
  }

  vkBindBufferMemory(s_Device, buffer, bufferMemory, 0);
}

VkDeviceAddress PathTracerCore::GetBufferDeviceAddress(VkBuffer buffer) {
  if (!pfn_vkGetBufferDeviceAddressKHR || buffer == VK_NULL_HANDLE) return 0;
  VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
  info.buffer = buffer;
  return pfn_vkGetBufferDeviceAddressKHR(s_Device, &info);
}

void PathTracerCore::CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
  VkCommandBuffer cmd;
  VkCommandBufferAllocateInfo allocInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocInfo.commandPool = s_CommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  vkAllocateCommandBuffers(s_Device, &allocInfo, &cmd);

  VkCommandBufferBeginInfo beginInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);

  VkBufferCopy copyRegion{0, 0, size};
  vkCmdCopyBuffer(cmd, src, dst, 1, &copyRegion);

  vkEndCommandBuffer(cmd);

  VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  vkQueueSubmit(s_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(s_GraphicsQueue);

  vkFreeCommandBuffers(s_Device, s_CommandPool, 1, &cmd);
}

void PathTracerCore::CreateImage(uint32_t width, uint32_t height,
                                 VkFormat format, VkImageTiling tiling,
                                 VkImageUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkImage &image, VkDeviceMemory &imageMemory) {
  VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = tiling;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(s_Device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(s_Device, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      FindMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(s_Device, &allocInfo, nullptr, &imageMemory) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate image memory!");
  }

  vkBindImageMemory(s_Device, image, imageMemory, 0);
}

void PathTracerCore::TransitionImageLayout(VkImage image, VkFormat format,
                                           VkImageLayout oldLayout,
                                           VkImageLayout newLayout) {
  VkCommandBuffer cmd;
  VkCommandBufferAllocateInfo allocInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocInfo.commandPool = s_CommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  vkAllocateCommandBuffers(s_Device, &allocInfo, &cmd);

  VkCommandBufferBeginInfo beginInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);

  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

  barrier.srcAccessMask = 0;
  barrier.dstAccessMask =
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);

  vkEndCommandBuffer(cmd);

  VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  vkQueueSubmit(s_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(s_GraphicsQueue);

  vkFreeCommandBuffers(s_Device, s_CommandPool, 1, &cmd);
}

uint32_t PathTracerCore::FindMemoryType(uint32_t typeFilter,
                                        VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(s_PhysicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }
  throw std::runtime_error("Failed to find suitable memory type!");
}

// ========== Hardware Ray Tracing (VK_KHR_ray_tracing_pipeline) Implementation ==========

static uint32_t AlignUp(uint32_t value, uint32_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

void PathTracerCore::InitHardwareRT() {
  if (!s_HardwareRTSupported) return;
  LOG_I("Initializing Hardware RT Pipeline & Resources...");
  CreateRTPipeline();
  CreateShaderBindingTable();
  LOG_I("Hardware RT Pipeline and SBT initialized.");
}

void PathTracerCore::ShutdownHardwareRT() {
  if (!s_HardwareRTSupported) return;
  DestroyAccelerationStructures();
  DestroyShaderBindingTable();
  DestroyRTPipeline();
  LOG_I("Hardware RT resources destroyed.");
}

void PathTracerCore::CreateRTPipeline() {
  // RT Descriptor Set Layout
  // Binding 0: TopLevelAS (Acceleration Structure)
  // Binding 1: Accum Image (Storage Image RGBA32F)
  // Binding 2: Triangles (Storage Buffer)
  // Binding 3: Materials (Storage Buffer)
  // Binding 4: Light Triangles (Storage Buffer)
  // Binding 5: Textures (Combined Image Sampler Array)
  // Binding 6: SkyParams (Uniform Buffer)
  // Binding 7: Normal & Depth G-Buffer (Storage Image RGBA16F)
  VkDescriptorSetLayoutBinding bindings[] = {
      {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,
       VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
       VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr},
      {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
       VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
      {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
       VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
      {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
       VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
      {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TextureManager::MAX_TEXTURES,
       VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, nullptr},
      {6, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
       VK_SHADER_STAGE_MISS_BIT_KHR, nullptr},
      {7, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
       VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr}};

  VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = 8;
  layoutInfo.pBindings = bindings;
  if (vkCreateDescriptorSetLayout(s_Device, &layoutInfo, nullptr, &s_RTDescriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create RT descriptor set layout!");
  }

  // Push Constants
  VkPushConstantRange pushRange{};
  pushRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                         VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                         VK_SHADER_STAGE_MISS_BIT_KHR;
  pushRange.offset = 0;
  pushRange.size = sizeof(PushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &s_RTDescriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushRange;
  if (vkCreatePipelineLayout(s_Device, &pipelineLayoutInfo, nullptr, &s_RTPipelineLayout) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create RT pipeline layout!");
  }

  // Load Shader Modules
  auto rgenCode = ReadSPV("resource/shaders/compiled/raytrace.rgen.spv");
  auto rmissCode = ReadSPV("resource/shaders/compiled/raytrace.rmiss.spv");
  auto rmissShadowCode = ReadSPV("resource/shaders/compiled/raytrace_shadow.rmiss.spv");
  auto chitDiffCode = ReadSPV("resource/shaders/compiled/raytrace_diffuse.rchit.spv");
  auto chitMetalCode = ReadSPV("resource/shaders/compiled/raytrace_metal.rchit.spv");
  auto chitGlassCode = ReadSPV("resource/shaders/compiled/raytrace_glass.rchit.spv");
  auto chitEmissCode = ReadSPV("resource/shaders/compiled/raytrace_emissive.rchit.spv");

  auto createShaderMod = [&](const std::vector<char>& code) {
    VkShaderModuleCreateInfo modInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    modInfo.codeSize = code.size();
    modInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod;
    if (vkCreateShaderModule(s_Device, &modInfo, nullptr, &mod) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create RT shader module!");
    }
    return mod;
  };

  VkShaderModule rgenMod = createShaderMod(rgenCode);
  VkShaderModule rmissMod = createShaderMod(rmissCode);
  VkShaderModule rmissShadowMod = createShaderMod(rmissShadowCode);
  VkShaderModule chitDiffMod = createShaderMod(chitDiffCode);
  VkShaderModule chitMetalMod = createShaderMod(chitMetalCode);
  VkShaderModule chitGlassMod = createShaderMod(chitGlassCode);
  VkShaderModule chitEmissMod = createShaderMod(chitEmissCode);

  std::vector<VkPipelineShaderStageCreateInfo> stages;
  auto addStage = [&](VkShaderModule mod, VkShaderStageFlagBits stage) {
    VkPipelineShaderStageCreateInfo s{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    s.stage = stage;
    s.module = mod;
    s.pName = "main";
    stages.push_back(s);
    return static_cast<uint32_t>(stages.size() - 1);
  };

  uint32_t sRgen = addStage(rgenMod, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
  uint32_t sMiss = addStage(rmissMod, VK_SHADER_STAGE_MISS_BIT_KHR);
  uint32_t sMissShadow = addStage(rmissShadowMod, VK_SHADER_STAGE_MISS_BIT_KHR);
  uint32_t sChit = addStage(chitDiffMod, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

  // Shader Groups
  std::vector<VkRayTracingShaderGroupCreateInfoKHR> groups;
  // Group 0: Raygen
  {
    VkRayTracingShaderGroupCreateInfoKHR g{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    g.generalShader = sRgen;
    g.closestHitShader = VK_SHADER_UNUSED_KHR;
    g.anyHitShader = VK_SHADER_UNUSED_KHR;
    g.intersectionShader = VK_SHADER_UNUSED_KHR;
    groups.push_back(g);
  }
  // Group 1: Miss Radiance
  {
    VkRayTracingShaderGroupCreateInfoKHR g{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    g.generalShader = sMiss;
    g.closestHitShader = VK_SHADER_UNUSED_KHR;
    g.anyHitShader = VK_SHADER_UNUSED_KHR;
    g.intersectionShader = VK_SHADER_UNUSED_KHR;
    groups.push_back(g);
  }
  // Group 2: Miss Shadow
  {
    VkRayTracingShaderGroupCreateInfoKHR g{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    g.generalShader = sMissShadow;
    g.closestHitShader = VK_SHADER_UNUSED_KHR;
    g.anyHitShader = VK_SHADER_UNUSED_KHR;
    g.intersectionShader = VK_SHADER_UNUSED_KHR;
    groups.push_back(g);
  }
  // Group 3: Hit (Unified Closest Hit)
  {
    VkRayTracingShaderGroupCreateInfoKHR g{VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
    g.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    g.generalShader = VK_SHADER_UNUSED_KHR;
    g.closestHitShader = sChit;
    g.anyHitShader = VK_SHADER_UNUSED_KHR;
    g.intersectionShader = VK_SHADER_UNUSED_KHR;
    groups.push_back(g);
  }

  VkRayTracingPipelineCreateInfoKHR pipelineInfo{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
  pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
  pipelineInfo.pStages = stages.data();
  pipelineInfo.groupCount = static_cast<uint32_t>(groups.size());
  pipelineInfo.pGroups = groups.data();
  pipelineInfo.maxPipelineRayRecursionDepth = 1; // Raygen handles bounces in iterative loop
  pipelineInfo.layout = s_RTPipelineLayout;

  if (pfn_vkCreateRayTracingPipelinesKHR(s_Device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &s_RTPipeline) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Ray Tracing pipeline!");
  }

  // Clean up shader modules
  vkDestroyShaderModule(s_Device, rgenMod, nullptr);
  vkDestroyShaderModule(s_Device, rmissMod, nullptr);
  vkDestroyShaderModule(s_Device, rmissShadowMod, nullptr);
  vkDestroyShaderModule(s_Device, chitDiffMod, nullptr);
  vkDestroyShaderModule(s_Device, chitMetalMod, nullptr);
  vkDestroyShaderModule(s_Device, chitGlassMod, nullptr);
  vkDestroyShaderModule(s_Device, chitEmissMod, nullptr);

  // Allocate RT Descriptor Set
  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = s_DescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &s_RTDescriptorSetLayout;
  if (vkAllocateDescriptorSets(s_Device, &allocInfo, &s_RTDescriptorSet) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate RT descriptor set!");
  }

  LOG_I("Ray Tracing Pipeline and Descriptor Set created successfully.");
}

void PathTracerCore::DestroyRTPipeline() {
  if (s_RTPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(s_Device, s_RTPipeline, nullptr);
    s_RTPipeline = VK_NULL_HANDLE;
  }
  if (s_RTPipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(s_Device, s_RTPipelineLayout, nullptr);
    s_RTPipelineLayout = VK_NULL_HANDLE;
  }
  if (s_RTDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(s_Device, s_RTDescriptorSetLayout, nullptr);
    s_RTDescriptorSetLayout = VK_NULL_HANDLE;
  }
}

void PathTracerCore::CreateShaderBindingTable() {
  uint32_t handleSize = s_RTProps.shaderGroupHandleSize;
  uint32_t handleAlignment = s_RTProps.shaderGroupHandleAlignment;
  uint32_t baseAlignment = s_RTProps.shaderGroupBaseAlignment;
  uint32_t handleSizeAligned = AlignUp(handleSize, handleAlignment);

  uint32_t groupCount = 4; // 1 raygen, 2 miss, 1 hit group
  uint32_t sbtSize = groupCount * handleSizeAligned;
  std::vector<uint8_t> shaderHandleStorage(sbtSize);

  if (pfn_vkGetRayTracingShaderGroupHandlesKHR(s_Device, s_RTPipeline, 0, groupCount, sbtSize, shaderHandleStorage.data()) != VK_SUCCESS) {
    throw std::runtime_error("Failed to get ray tracing shader group handles!");
  }

  // SBT Regions calculation:
  // Raygen: 1 record
  s_RaygenRegion.stride = AlignUp(handleSizeAligned, baseAlignment);
  s_RaygenRegion.size = s_RaygenRegion.stride;

  // Miss: 2 records (radiance, shadow)
  s_MissRegion.stride = handleSizeAligned;
  s_MissRegion.size = AlignUp(2 * handleSizeAligned, baseAlignment);

  // Hit: 1 record (unified hit group)
  s_HitRegion.stride = AlignUp(handleSizeAligned, baseAlignment);
  s_HitRegion.size = s_HitRegion.stride;

  s_CallableRegion = VkStridedDeviceAddressRegionKHR{};

  VkDeviceSize totalSBTSize = s_RaygenRegion.size + s_MissRegion.size + s_HitRegion.size;

  if (s_SBTBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(s_Device, s_SBTBuffer, nullptr);
    vkFreeMemory(s_Device, s_SBTBufferMemory, nullptr);
    s_SBTBuffer = VK_NULL_HANDLE;
  }

  CreateBuffer(totalSBTSize,
               VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
               s_SBTBuffer, s_SBTBufferMemory);

  // Prepare staged upload buffer
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingMemory;
  CreateBuffer(totalSBTSize,
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingMemory);

  uint8_t* pData = nullptr;
  vkMapMemory(s_Device, stagingMemory, 0, totalSBTSize, 0, (void**)&pData);
  memset(pData, 0, totalSBTSize);

  // Copy Raygen (Group 0)
  memcpy(pData, shaderHandleStorage.data() + 0 * handleSizeAligned, handleSize);

  // Copy Miss (Group 1 & 2)
  uint8_t* pMiss = pData + s_RaygenRegion.size;
  memcpy(pMiss + 0 * handleSizeAligned, shaderHandleStorage.data() + 1 * handleSizeAligned, handleSize);
  memcpy(pMiss + 1 * handleSizeAligned, shaderHandleStorage.data() + 2 * handleSizeAligned, handleSize);

  // Copy Hit Group (Group 3: Unified Hit)
  uint8_t* pHit = pData + s_RaygenRegion.size + s_MissRegion.size;
  memcpy(pHit + 0 * handleSizeAligned, shaderHandleStorage.data() + 3 * handleSizeAligned, handleSize);

  vkUnmapMemory(s_Device, stagingMemory);

  CopyBuffer(stagingBuffer, s_SBTBuffer, totalSBTSize);

  vkDestroyBuffer(s_Device, stagingBuffer, nullptr);
  vkFreeMemory(s_Device, stagingMemory, nullptr);

  VkDeviceAddress sbtAddress = GetBufferDeviceAddress(s_SBTBuffer);
  s_RaygenRegion.deviceAddress = sbtAddress;
  s_MissRegion.deviceAddress = sbtAddress + s_RaygenRegion.size;
  s_HitRegion.deviceAddress = sbtAddress + s_RaygenRegion.size + s_MissRegion.size;

  LOG_I("SBT Buffer created and mapped at 0x{:x}", sbtAddress);
}

void PathTracerCore::DestroyShaderBindingTable() {
  if (s_SBTBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(s_Device, s_SBTBuffer, nullptr);
    vkFreeMemory(s_Device, s_SBTBufferMemory, nullptr);
    s_SBTBuffer = VK_NULL_HANDLE;
  }
}

void PathTracerCore::DestroyAccelerationStructures() {
  if (s_TLAS != VK_NULL_HANDLE) {
    pfn_vkDestroyAccelerationStructureKHR(s_Device, s_TLAS, nullptr);
    s_TLAS = VK_NULL_HANDLE;
  }
  if (s_TLASBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(s_Device, s_TLASBuffer, nullptr);
    vkFreeMemory(s_Device, s_TLASBufferMemory, nullptr);
    s_TLASBuffer = VK_NULL_HANDLE;
  }
  if (s_BLAS != VK_NULL_HANDLE) {
    pfn_vkDestroyAccelerationStructureKHR(s_Device, s_BLAS, nullptr);
    s_BLAS = VK_NULL_HANDLE;
  }
  if (s_BLASBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(s_Device, s_BLASBuffer, nullptr);
    vkFreeMemory(s_Device, s_BLASBufferMemory, nullptr);
    s_BLASBuffer = VK_NULL_HANDLE;
  }
  if (s_InstanceBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(s_Device, s_InstanceBuffer, nullptr);
    vkFreeMemory(s_Device, s_InstanceBufferMemory, nullptr);
    s_InstanceBuffer = VK_NULL_HANDLE;
  }
  if (s_RTVertexBuffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(s_Device, s_RTVertexBuffer, nullptr);
    vkFreeMemory(s_Device, s_RTVertexBufferMemory, nullptr);
    s_RTVertexBuffer = VK_NULL_HANDLE;
  }
}

void PathTracerCore::BuildAccelerationStructures() {
  if (!s_HardwareRTSupported || s_TriangleBuffer == VK_NULL_HANDLE) return;

  const auto& triangles = s_Scene.GetBVH().GetTriangles();
  if (triangles.empty()) return;

  uint32_t triangleCount = static_cast<uint32_t>(triangles.size());
  uint32_t vertexCount = triangleCount * 3;

  DestroyAccelerationStructures();

  // 1. Pack contiguous vertex positions (v0, v1, v2) for all triangles
  std::vector<glm::vec3> positions;
  positions.reserve(vertexCount);
  for (const auto& tri : triangles) {
    positions.push_back(glm::vec3(tri.v0));
    positions.push_back(glm::vec3(tri.v1));
    positions.push_back(glm::vec3(tri.v2));
  }

  VkDeviceSize posBufferSize = sizeof(glm::vec3) * positions.size();
  CreateBuffer(posBufferSize,
               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
               s_RTVertexBuffer, s_RTVertexBufferMemory);

  // Staging upload for vertex positions
  VkBuffer posStaging;
  VkDeviceMemory posStagingMem;
  CreateBuffer(posBufferSize,
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               posStaging, posStagingMem);
  void* posMap = nullptr;
  vkMapMemory(s_Device, posStagingMem, 0, posBufferSize, 0, &posMap);
  memcpy(posMap, positions.data(), posBufferSize);
  vkUnmapMemory(s_Device, posStagingMem);
  CopyBuffer(posStaging, s_RTVertexBuffer, posBufferSize);
  vkDestroyBuffer(s_Device, posStaging, nullptr);
  vkFreeMemory(s_Device, posStagingMem, nullptr);

  VkDeviceAddress posAddress = GetBufferDeviceAddress(s_RTVertexBuffer);

  // 1. BLAS Geometry: Triangles
  VkAccelerationStructureGeometryKHR geom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
  geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
  geom.geometry.triangles.vertexData.deviceAddress = posAddress;
  geom.geometry.triangles.vertexStride = sizeof(glm::vec3); // Contiguous 3-float vertices
  geom.geometry.triangles.maxVertex = vertexCount;
  geom.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;

  VkAccelerationStructureBuildGeometryInfoKHR blasBuildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  blasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  blasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  blasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  blasBuildInfo.geometryCount = 1;
  blasBuildInfo.pGeometries = &geom;

  VkAccelerationStructureBuildSizesInfoKHR blasSizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  pfn_vkGetAccelerationStructureBuildSizesKHR(s_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &blasBuildInfo, &triangleCount, &blasSizeInfo);

  // Allocate BLAS buffer
  CreateBuffer(blasSizeInfo.accelerationStructureSize,
               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_BLASBuffer, s_BLASBufferMemory);

  VkAccelerationStructureCreateInfoKHR blasCreateInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  blasCreateInfo.buffer = s_BLASBuffer;
  blasCreateInfo.size = blasSizeInfo.accelerationStructureSize;
  blasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  if (pfn_vkCreateAccelerationStructureKHR(s_Device, &blasCreateInfo, nullptr, &s_BLAS) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create BLAS!");
  }

  // Allocate scratch buffer for BLAS
  VkBuffer blasScratchBuffer;
  VkDeviceMemory blasScratchMemory;
  CreateBuffer(blasSizeInfo.buildScratchSize,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, blasScratchBuffer, blasScratchMemory);

  blasBuildInfo.dstAccelerationStructure = s_BLAS;
  blasBuildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(blasScratchBuffer);

  // 2. TLAS Instances
  VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
  blasAddrInfo.accelerationStructure = s_BLAS;
  VkDeviceAddress blasAddress = pfn_vkGetAccelerationStructureDeviceAddressKHR(s_Device, &blasAddrInfo);

  VkTransformMatrixKHR identityMatrix = {
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f
  };

  VkAccelerationStructureInstanceKHR instance{};
  instance.transform = identityMatrix;
  instance.instanceCustomIndex = 0;
  instance.mask = 0xFF;
  instance.instanceShaderBindingTableRecordOffset = 0; // Default offset
  instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  instance.accelerationStructureReference = blasAddress;

  CreateBuffer(sizeof(VkAccelerationStructureInstanceKHR),
               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_InstanceBuffer, s_InstanceBufferMemory);

  // Staging for instance
  VkBuffer instStaging;
  VkDeviceMemory instStagingMem;
  CreateBuffer(sizeof(VkAccelerationStructureInstanceKHR),
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               instStaging, instStagingMem);
  void* instMap = nullptr;
  vkMapMemory(s_Device, instStagingMem, 0, sizeof(instance), 0, &instMap);
  memcpy(instMap, &instance, sizeof(instance));
  vkUnmapMemory(s_Device, instStagingMem);
  CopyBuffer(instStaging, s_InstanceBuffer, sizeof(instance));
  vkDestroyBuffer(s_Device, instStaging, nullptr);
  vkFreeMemory(s_Device, instStagingMem, nullptr);

  VkDeviceAddress instAddress = GetBufferDeviceAddress(s_InstanceBuffer);

  VkAccelerationStructureGeometryKHR tlasGeom{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
  tlasGeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  tlasGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  tlasGeom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  tlasGeom.geometry.instances.arrayOfPointers = VK_FALSE;
  tlasGeom.geometry.instances.data.deviceAddress = instAddress;

  VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
  tlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  tlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  tlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  tlasBuildInfo.geometryCount = 1;
  tlasBuildInfo.pGeometries = &tlasGeom;

  uint32_t instanceCount = 1;
  VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
  pfn_vkGetAccelerationStructureBuildSizesKHR(s_Device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                              &tlasBuildInfo, &instanceCount, &tlasSizeInfo);

  CreateBuffer(tlasSizeInfo.accelerationStructureSize,
               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_TLASBuffer, s_TLASBufferMemory);

  VkAccelerationStructureCreateInfoKHR tlasCreateInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
  tlasCreateInfo.buffer = s_TLASBuffer;
  tlasCreateInfo.size = tlasSizeInfo.accelerationStructureSize;
  tlasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  if (pfn_vkCreateAccelerationStructureKHR(s_Device, &tlasCreateInfo, nullptr, &s_TLAS) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create TLAS!");
  }

  VkBuffer tlasScratchBuffer;
  VkDeviceMemory tlasScratchMemory;
  CreateBuffer(tlasSizeInfo.buildScratchSize,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tlasScratchBuffer, tlasScratchMemory);

  tlasBuildInfo.dstAccelerationStructure = s_TLAS;
  tlasBuildInfo.scratchData.deviceAddress = GetBufferDeviceAddress(tlasScratchBuffer);

  // Execute Build Commands on GPU
  VkCommandBuffer cmd;
  VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocInfo.commandPool = s_CommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  vkAllocateCommandBuffers(s_Device, &allocInfo, &cmd);

  VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);

  // 1. Build BLAS
  VkAccelerationStructureBuildRangeInfoKHR blasRange{triangleCount, 0, 0, 0};
  const VkAccelerationStructureBuildRangeInfoKHR* pBlasRange = &blasRange;
  pfn_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &blasBuildInfo, &pBlasRange);

  // Barrier between BLAS build and TLAS build
  VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
  barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       0, 1, &barrier, 0, nullptr, 0, nullptr);

  // 2. Build TLAS
  VkAccelerationStructureBuildRangeInfoKHR tlasRange{1, 0, 0, 0};
  const VkAccelerationStructureBuildRangeInfoKHR* pTlasRange = &tlasRange;
  pfn_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlasBuildInfo, &pTlasRange);

  vkEndCommandBuffer(cmd);

  VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  vkQueueSubmit(s_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(s_GraphicsQueue);

  vkFreeCommandBuffers(s_Device, s_CommandPool, 1, &cmd);

  // Destroy scratch buffers
  vkDestroyBuffer(s_Device, blasScratchBuffer, nullptr);
  vkFreeMemory(s_Device, blasScratchMemory, nullptr);
  vkDestroyBuffer(s_Device, tlasScratchBuffer, nullptr);
  vkFreeMemory(s_Device, tlasScratchMemory, nullptr);

  // Update RT Descriptor Sets
  VkWriteDescriptorSetAccelerationStructureKHR asInfo{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
  asInfo.accelerationStructureCount = 1;
  asInfo.pAccelerationStructures = &s_TLAS;

  VkDescriptorImageInfo accumImgInfo{};
  accumImgInfo.imageView = s_AccumImageView;
  accumImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  VkDescriptorBufferInfo triBufInfo{s_TriangleBuffer, 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo matBufInfo{s_MaterialBuffer, 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo lightBufInfo{s_LightBuffer, 0, VK_WHOLE_SIZE};
  auto texInfos = TextureManager::Instance().GetDescriptorImageInfos();
  VkDescriptorBufferInfo skyBufInfo{s_SkyBuffer, 0, sizeof(SkyUBO)};

  VkDescriptorImageInfo normalDepthImgInfo{};
  normalDepthImgInfo.imageView = s_NormalDepthImageView;
  normalDepthImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  VkWriteDescriptorSet descriptorWrites[] = {
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &asInfo, s_RTDescriptorSet,
       0, 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, nullptr, nullptr, nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_RTDescriptorSet,
       1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &accumImgInfo, nullptr, nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_RTDescriptorSet,
       2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &triBufInfo, nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_RTDescriptorSet,
       3, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &matBufInfo, nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_RTDescriptorSet,
       4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &lightBufInfo, nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_RTDescriptorSet,
       5, 0, TextureManager::MAX_TEXTURES, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texInfos.data(), nullptr, nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_RTDescriptorSet,
       6, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &skyBufInfo, nullptr},
      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, s_RTDescriptorSet,
       7, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &normalDepthImgInfo, nullptr, nullptr}};

  vkUpdateDescriptorSets(s_Device, 8, descriptorWrites, 0, nullptr);

  LOG_I("Hardware Acceleration Structures (BLAS & TLAS) built successfully.");
}

void PathTracerCore::DispatchHardwareRT(VkCommandBuffer cmd) {
  UpdateSkyUBO();

  // Bind RT Pipeline
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, s_RTPipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
                          s_RTPipelineLayout, 0, 1,
                          &s_RTDescriptorSet, 0, nullptr);

  // Push Constants
  PushConstants pc{};
  pc.camPos = glm::vec4(s_Camera.GetPosition(), glm::radians(s_Camera.GetFov()));
  pc.camFront = glm::vec4(s_Camera.GetForward(), s_Camera.GetAperture());
  pc.camRight = glm::vec4(s_Camera.GetRight(), s_Camera.GetFocusDistance());
  pc.camUp = glm::vec4(s_Camera.GetUp(), static_cast<float>(s_MaxBounces));
  pc.renderParams = glm::uvec4(s_RenderWidth, s_RenderHeight, s_FrameIndex,
                               static_cast<uint32_t>(s_SamplesPerFrame));
  pc.envAndTone = glm::vec4(s_EnvColor * s_EnvIntensity, static_cast<float>(s_TonemapMode));
  uint32_t hostSeed = static_cast<uint32_t>(s_HostRng());
  float hostSeedFloat;
  std::memcpy(&hostSeedFloat, &hostSeed, sizeof(float));
  pc.postParams = glm::vec4(s_Gamma, s_Exposure, hostSeedFloat,
                            static_cast<float>(s_Scene.GetLightTriangles().size()));

  vkCmdPushConstants(cmd, s_RTPipelineLayout,
                     VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                         VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                         VK_SHADER_STAGE_MISS_BIT_KHR,
                     0, sizeof(PushConstants), &pc);

  // Trace Rays
  pfn_vkCmdTraceRaysKHR(cmd,
                        &s_RaygenRegion,
                        &s_MissRegion,
                        &s_HitRegion,
                        &s_CallableRegion,
                        s_RenderWidth, s_RenderHeight, 1);

  // Barrier on s_AccumImage so subsequent Denoise/Bloom/PostProcess read new samples
  RecordImageBarrier(cmd, s_AccumImage, VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

} // namespace neurender
