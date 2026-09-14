/* TextureCache.h defines a bounded, explicit Vulkan image cache for T# descriptors. */
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace aceps::gpu {

// Kept opaque so AcePS remains buildable without requiring the optional VMA SDK.
using VmaAllocator = void*;

class TextureCache final {
public:
  TextureCache() = default;
  ~TextureCache();
  TextureCache(const TextureCache&) = delete;
  TextureCache& operator=(const TextureCache&) = delete;

  [[nodiscard]] bool init(VkDevice device, VkPhysicalDevice physicalDevice,
                          VmaAllocator allocator, std::string& error);
  void destroy() noexcept;
  [[nodiscard]] VkImageView getOrUpload(const std::uint32_t tsharp[8],
                                        VkCommandBuffer uploadCommand,
                                        std::string& error);
  void invalidateRange(std::uint64_t address, std::uint64_t size) noexcept;

private:
  struct Entry final {
    std::uint64_t address{0};
    std::uint64_t size{0};
    VkImage image{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
  };
  struct Decoded final {
    std::uint64_t address{0};
    std::uint32_t width{1};
    std::uint32_t height{1};
    std::uint32_t mipLevels{1};
    VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
  };

  [[nodiscard]] bool decode(const std::uint32_t tsharp[8], Decoded& output,
                            std::string& error) const;
  [[nodiscard]] std::uint32_t findMemoryType(std::uint32_t bits,
                                              VkMemoryPropertyFlags properties) const noexcept;
  VkFormat formatFor(std::uint32_t dataFormat, std::uint32_t numberFormat) const noexcept;
  void destroyEntry(Entry& entry) noexcept;

  VkDevice device_{VK_NULL_HANDLE};
  VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
  VmaAllocator allocator_{nullptr};
  std::unordered_map<std::uint64_t, Entry> entries_;
};

} // namespace aceps::gpu
