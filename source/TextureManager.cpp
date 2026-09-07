#include "TextureManager.h"
#include "neuLog.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <cstring>
#include <stdexcept>
#include <filesystem>
#include "imgui_impl_vulkan.h"

namespace neurender {

void TextureManager::Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue) {
    m_Device = device;
    m_PhysicalDevice = physicalDevice;
    m_CommandPool = commandPool;
    m_Queue = queue;

    // Create shared bilinear repeat sampler
    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create TextureManager sampler!");
    }

    m_Textures.clear();
    m_PathToIndex.clear();
    m_Initialized = true;

    CreateDefaultTextures();
    LOG_I("TextureManager initialized with {} default textures.", m_Textures.size());
}

void TextureManager::CreateDefaultTextures() {
    // Slot 0: 1x1 White (1.0, 1.0, 1.0, 1.0)
    uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    CreateTextureFromMemory("__default_white__", whitePixel, 1, 1, false);

    // Slot 1: 1x1 Flat Normal (0.5, 0.5, 1.0) -> RGB (128, 128, 255)
    uint8_t flatNormalPixel[4] = { 128, 128, 255, 255 };
    CreateTextureFromMemory("__default_normal__", flatNormalPixel, 1, 1, false);

    // Slot 2: 1x1 Roughness Mid (0.5) -> (128, 128, 128)
    uint8_t midRoughPixel[4] = { 128, 128, 128, 255 };
    CreateTextureFromMemory("__default_roughness__", midRoughPixel, 1, 1, false);
}

void TextureManager::Shutdown() {
    if (!m_Initialized) return;

    for (auto& tex : m_Textures) {
        if (tex.view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, tex.view, nullptr);
            tex.view = VK_NULL_HANDLE;
        }
        if (tex.image != VK_NULL_HANDLE) {
            vkDestroyImage(m_Device, tex.image, nullptr);
            vkFreeMemory(m_Device, tex.memory, nullptr);
            tex.image = VK_NULL_HANDLE;
            tex.memory = VK_NULL_HANDLE;
        }
    }
    m_Textures.clear();
    m_PathToIndex.clear();

    if (m_Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_Device, m_Sampler, nullptr);
        m_Sampler = VK_NULL_HANDLE;
    }

    m_Initialized = false;
    LOG_I("TextureManager shut down.");
}

int TextureManager::LoadTexture(const std::string& filepath, bool isSRGB) {
    if (!m_Initialized || filepath.empty()) return -1;

    auto it = m_PathToIndex.find(filepath);
    if (it != m_PathToIndex.end()) {
        return it->second;
    }

    if (m_Textures.size() >= MAX_TEXTURES) {
        LOG_W("TextureManager: maximum texture limit ({}) reached!", MAX_TEXTURES);
        return -1;
    }

    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(filepath.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        LOG_W("TextureManager: failed to load image from: {}", filepath);
        return -1;
    }

    int slot = CreateTextureFromMemory(filepath, pixels, w, h, isSRGB);
    stbi_image_free(pixels);

    LOG_I("Loaded texture '{}' into slot {} ({}x{}).", filepath, slot, w, h);
    return slot;
}

int TextureManager::CreateTextureFromMemory(const std::string& name, const uint8_t* rgba, int w, int h, bool isSRGB) {
    if (!m_Initialized || !rgba || w <= 0 || h <= 0) return -1;

    if (m_Textures.size() >= MAX_TEXTURES) {
        LOG_W("TextureManager: maximum texture limit ({}) reached!", MAX_TEXTURES);
        return -1;
    }

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingMemory);

    void* data;
    vkMapMemory(m_Device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, rgba, static_cast<size_t>(imageSize));
    vkUnmapMemory(m_Device, stagingMemory);

    VkFormat format = isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    // Create Image
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(w);
    imageInfo.extent.height = static_cast<uint32_t>(h);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    TextureResource res;
    res.path = name;
    res.width = w;
    res.height = h;
    res.channels = 4;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &res.image) != VK_SUCCESS) {
        vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
        vkFreeMemory(m_Device, stagingMemory, nullptr);
        throw std::runtime_error("Failed to create texture image!");
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_Device, res.image, &memReqs);

    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &res.memory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate texture memory!");
    }
    vkBindImageMemory(m_Device, res.image, res.memory, 0);

    // Command buffer for transition & copy
    VkCommandBufferAllocateInfo cmdAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool = m_CommandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(m_Device, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    // Transition to TRANSFER_DST_OPTIMAL
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = res.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };

    vkCmdCopyBufferToImage(cmd, stagingBuffer, res.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to SHADER_READ_ONLY_OPTIMAL
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_Queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_Queue);

    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &cmd);
    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
    vkFreeMemory(m_Device, stagingMemory, nullptr);

    // Create Image View
    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = res.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &res.view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image view!");
    }

    int slot = static_cast<int>(m_Textures.size());
    m_Textures.push_back(res);
    m_PathToIndex[name] = slot;
    return slot;
}

const TextureResource* TextureManager::GetTexture(int index) const {
    if (index >= 0 && index < static_cast<int>(m_Textures.size())) {
        return &m_Textures[index];
    }
    return nullptr;
}

std::vector<VkDescriptorImageInfo> TextureManager::GetDescriptorImageInfos() const {
    std::vector<VkDescriptorImageInfo> infos(MAX_TEXTURES);
    VkImageView defaultView = m_Textures.empty() ? VK_NULL_HANDLE : m_Textures[0].view;

    for (uint32_t i = 0; i < MAX_TEXTURES; ++i) {
        infos[i].sampler = m_Sampler;
        infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (i < m_Textures.size() && m_Textures[i].view != VK_NULL_HANDLE) {
            infos[i].imageView = m_Textures[i].view;
        } else {
            infos[i].imageView = defaultView;
        }
    }
    return infos;
}

void TextureManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties, VkBuffer& buffer,
                                  VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_Device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }
    vkBindBufferMemory(m_Device, buffer, bufferMemory, 0);
}

uint32_t TextureManager::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

UIPreviewTexture TextureManager::CreateUIPreviewTexture(const std::string& filepath) {
    UIPreviewTexture preview{};
    if (!m_Initialized || filepath.empty()) return preview;

    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::u8path(filepath), ec)) {
        return preview;
    }

    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(filepath.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return preview;
    }

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    try {
        CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stagingBuffer, stagingMemory);
    } catch (...) {
        stbi_image_free(pixels);
        return preview;
    }

    void* data = nullptr;
    vkMapMemory(m_Device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(m_Device, stagingMemory);
    stbi_image_free(pixels);

    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(w);
    imageInfo.extent.height = static_cast<uint32_t>(h);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &preview.image) != VK_SUCCESS) {
        vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
        vkFreeMemory(m_Device, stagingMemory, nullptr);
        return preview;
    }

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_Device, preview.image, &memReqs);

    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &preview.memory) != VK_SUCCESS) {
        vkDestroyImage(m_Device, preview.image, nullptr);
        preview.image = VK_NULL_HANDLE;
        vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
        vkFreeMemory(m_Device, stagingMemory, nullptr);
        return preview;
    }
    vkBindImageMemory(m_Device, preview.image, preview.memory, 0);

    VkCommandBufferAllocateInfo cmdAlloc{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cmdAlloc.commandPool = m_CommandPool;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_Device, &cmdAlloc, &cmd);

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = preview.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };

    vkCmdCopyBufferToImage(cmd, stagingBuffer, preview.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(m_Queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_Queue);

    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &cmd);
    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
    vkFreeMemory(m_Device, stagingMemory, nullptr);

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = preview.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &preview.view) != VK_SUCCESS) {
        vkDestroyImage(m_Device, preview.image, nullptr);
        vkFreeMemory(m_Device, preview.memory, nullptr);
        preview.image = VK_NULL_HANDLE;
        preview.memory = VK_NULL_HANDLE;
        return preview;
    }

    preview.descriptorSet = ImGui_ImplVulkan_AddTexture(m_Sampler, preview.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    preview.width = w;
    preview.height = h;
    preview.channels = channels;
    preview.path = filepath;

    return preview;
}

void TextureManager::DestroyUIPreviewTexture(UIPreviewTexture& tex) {
    if (!m_Initialized || tex.image == VK_NULL_HANDLE) {
        return;
    }
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);

        if (tex.descriptorSet != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(tex.descriptorSet);
            tex.descriptorSet = VK_NULL_HANDLE;
        }
        if (tex.view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_Device, tex.view, nullptr);
            tex.view = VK_NULL_HANDLE;
        }
        if (tex.image != VK_NULL_HANDLE) {
            vkDestroyImage(m_Device, tex.image, nullptr);
            tex.image = VK_NULL_HANDLE;
        }
        if (tex.memory != VK_NULL_HANDLE) {
            vkFreeMemory(m_Device, tex.memory, nullptr);
            tex.memory = VK_NULL_HANDLE;
        }
    }
    tex.width = 0;
    tex.height = 0;
    tex.channels = 0;
    tex.path.clear();
}

} // namespace neurender
