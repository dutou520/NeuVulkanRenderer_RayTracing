#include "PathTracerCore.h"
#include "TextureManager.h"
#include "Window.h"
#include "neuLog.h"
#include <SDL3/SDL_vulkan.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <stdexcept>

namespace neurender {

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
std::vector<VkFramebuffer> PathTracerCore::s_SwapchainFramebuffers;

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

int PathTracerCore::s_SkyMode = 0;            // 0 = const, 1 = Nishita 1993
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
  CreateSwapchain();
  LOG_I("Step: CreateSwapchain done");
  CreateSwapchainImageViews();
  LOG_I("Step: CreateSwapchainImageViews done");
  CreateRenderPass();
  LOG_I("Step: CreateRenderPass done");
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

  CreateComputePipeline();
  LOG_I("Step: CreateComputePipeline done");

  CreateComputeResources();
  LOG_I("Step: CreateComputeResources done");

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

  DestroyComputeResources();

  DestroyPostProcessPipeline();
  DestroyBloomPipelines();
  DestroyDenoisePipeline();

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
  vkDestroyRenderPass(s_Device, s_UIRenderPass, nullptr);
  for (auto iv : s_SwapchainImageViews)
    vkDestroyImageView(s_Device, iv, nullptr);
  vkDestroySwapchainKHR(s_Device, s_Swapchain, nullptr);

  vkDestroyDevice(s_Device, nullptr);
  vkDestroySurfaceKHR(s_Instance, s_Surface, nullptr);
  vkDestroyInstance(s_Instance, nullptr);

  LOG_I("PathTracerCore shut down cleanly.");
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

  std::vector<const char *> deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
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

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = s_Surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = s_SwapchainImageFormat;
  createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
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

  LOG_I("Swapchain created ({} images, {}x{}).", imageCount,
        s_SwapchainExtent.width, s_SwapchainExtent.height);
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

  if (vkCreateRenderPass(s_Device, &renderPassInfo, nullptr, &s_UIRenderPass) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create UI render pass!");
  }
}

void PathTracerCore::CreateFramebuffers() {
  s_SwapchainFramebuffers.resize(s_SwapchainImageViews.size());
  for (size_t i = 0; i < s_SwapchainImageViews.size(); ++i) {
    VkImageView attachments[] = {s_SwapchainImageViews[i]};
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = s_UIRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = attachments;
    fbInfo.width = s_SwapchainExtent.width;
    fbInfo.height = s_SwapchainExtent.height;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(s_Device, &fbInfo, nullptr,
                            &s_SwapchainFramebuffers[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create framebuffer!");
    }
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
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100}};

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

  // 2. Display Image (RGBA8 UNORM)
  CreateImage(s_RenderWidth, s_RenderHeight, VK_FORMAT_R8G8B8A8_UNORM,
              VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, s_DisplayImage,
              s_DisplayImageMemory);

  VkImageViewCreateInfo dispViewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  dispViewInfo.image = s_DisplayImage;
  dispViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  dispViewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  dispViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  dispViewInfo.subresourceRange.levelCount = 1;
  dispViewInfo.subresourceRange.layerCount = 1;
  vkCreateImageView(s_Device, &dispViewInfo, nullptr, &s_DisplayImageView);

  TransitionImageLayout(s_DisplayImage, VK_FORMAT_R8G8B8A8_UNORM,
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
  pushRange.size = 32; // 8 x 4 bytes

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
  } ppParams;

  ppParams.exposure = s_Exposure;
  ppParams.gamma = s_Gamma;
  ppParams.tonemapMode = s_TonemapMode;
  ppParams.bloomEnabled = s_BloomEnabled ? 1 : 0;
  ppParams.bloomIntensity = s_BloomIntensity;
  ppParams.invSPP = 1.0f / std::max(1.0f, static_cast<float>(s_FrameIndex));
  ppParams.width = static_cast<int>(s_RenderWidth);
  ppParams.height = static_cast<int>(s_RenderHeight);

  vkCmdPushConstants(cmd, s_PostProcessPipelineLayout,
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ppParams),
                     &ppParams);

  // 4. Dispatch
  vkCmdDispatch(cmd, (s_RenderWidth + 15) / 16, (s_RenderHeight + 15) / 16, 1);

  // 5. Transition display image back to SHADER_READ_ONLY_OPTIMAL for ImGui
  // Viewport sampling
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
    CreateBuffer(triSize,
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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

  // Dispatch Compute Path Tracer if target SPP not reached
  if (s_TargetSPP <= 0 || static_cast<int>(s_FrameIndex) < s_TargetSPP) {
    DispatchCompute(cmd);
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

  // UI RenderPass
  VkRenderPassBeginInfo renderPassInfo{
      VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  renderPassInfo.renderPass = s_UIRenderPass;
  renderPassInfo.framebuffer = s_SwapchainFramebuffers[imageIndex];
  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = s_SwapchainExtent;

  VkClearValue clearColor = {{{0.1f, 0.1f, 0.12f, 1.0f}}};
  renderPassInfo.clearValueCount = 1;
  renderPassInfo.pClearValues = &clearColor;

  vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
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

  for (auto fb : s_SwapchainFramebuffers)
    vkDestroyFramebuffer(s_Device, fb, nullptr);
  for (auto iv : s_SwapchainImageViews)
    vkDestroyImageView(s_Device, iv, nullptr);
  vkDestroySwapchainKHR(s_Device, s_Swapchain, nullptr);

  CreateSwapchain();
  CreateSwapchainImageViews();
  CreateFramebuffers();
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

  VkDeviceSize imageSize = width * height * 4;
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

  // Write PNG
  void *mappedData;
  vkMapMemory(s_Device, stagingBufferMemory, 0, imageSize, 0, &mappedData);

  if (captureUI) {
    // Swap B and R for B8G8R8A8 format
    uint8_t *pixels = static_cast<uint8_t *>(mappedData);
    for (size_t i = 0; i < width * height; ++i) {
      uint8_t b = pixels[i * 4 + 0];
      uint8_t r = pixels[i * 4 + 2];
      pixels[i * 4 + 0] = r;
      pixels[i * 4 + 2] = b;
      pixels[i * 4 + 3] = 255;
    }
  }

  stbi_flip_vertically_on_write(0);
  int success = stbi_write_png(path.c_str(), static_cast<int>(width),
                               static_cast<int>(height), 4, mappedData,
                               static_cast<int>(width * 4));
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

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      FindMemoryType(memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(s_Device, &allocInfo, nullptr, &bufferMemory) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate buffer memory!");
  }

  vkBindBufferMemory(s_Device, buffer, bufferMemory, 0);
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

} // namespace neurender
