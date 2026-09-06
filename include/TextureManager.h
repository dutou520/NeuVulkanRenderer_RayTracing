#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace neurender {

struct TextureResource {
    std::string path;
    int width = 0;
    int height = 0;
    int channels = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

class TextureManager {
public:
    static constexpr uint32_t MAX_TEXTURES = 64;

    static TextureManager& Instance() {
        static TextureManager instance;
        return instance;
    }

    void Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue queue);
    void Shutdown();

    // Loads texture from file, returns slot index (0..MAX_TEXTURES-1). If failed, returns -1.
    int LoadTexture(const std::string& filepath, bool isSRGB = false);

    // Creates an in-memory RGBA texture and registers it
    int CreateTextureFromMemory(const std::string& name, const uint8_t* rgba, int w, int h, bool isSRGB = false);

    int GetTextureCount() const { return static_cast<int>(m_Textures.size()); }
    const TextureResource* GetTexture(int index) const;

    VkSampler GetSampler() const { return m_Sampler; }

    // Fills an array of VkDescriptorImageInfo with MAX_TEXTURES entries for descriptor writes
    std::vector<VkDescriptorImageInfo> GetDescriptorImageInfos() const;

private:
    TextureManager() = default;
    ~TextureManager() = default;

    void CreateDefaultTextures();
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkDevice m_Device = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    VkQueue m_Queue = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;

    std::vector<TextureResource> m_Textures;
    std::unordered_map<std::string, int> m_PathToIndex;
    bool m_Initialized = false;
};

} // namespace neurender
