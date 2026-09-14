#include "aceps/gpu/TextureCache.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include <algorithm>
#include <limits>

namespace aceps::gpu {

TextureCache::~TextureCache() { destroy(); }

bool TextureCache::init(const VkDevice device, const VkPhysicalDevice physicalDevice,
                        const VmaAllocator allocator, std::string& error) {
  destroy();
  if (device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
    error = "texture cache requires valid Vulkan handles";
    return false;
  }
  device_ = device;
  physicalDevice_ = physicalDevice;
  allocator_ = allocator;
  error.clear();
  return true;
}

VkFormat TextureCache::formatFor(const std::uint32_t dataFormat, const std::uint32_t numberFormat) const noexcept {
  switch (dataFormat) {
  case 0x08U: return VK_FORMAT_B8G8R8A8_UNORM;
  case 0x0AU: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
  case 0x0BU: return VK_FORMAT_BC2_UNORM_BLOCK;
  case 0x0CU: return VK_FORMAT_BC3_UNORM_BLOCK;
  case 0x1AU: return numberFormat == 4U ? VK_FORMAT_R8G8B8A8_UINT : VK_FORMAT_R8G8B8A8_UNORM;
  case 0x35U: return VK_FORMAT_R16G16B16A16_SFLOAT;
  case 0x39U: return VK_FORMAT_R32G32B32A32_SFLOAT;
  default: return VK_FORMAT_R8G8B8A8_UNORM;
  }
}

bool TextureCache::decode(const std::uint32_t tsharp[8], Decoded& output, std::string& error) const {
  if (tsharp == nullptr) {
    error = "null T# descriptor";
    return false;
  }
  const std::uint64_t low = tsharp[0];
  const std::uint64_t high = tsharp[1];
  output.address = ((low | (high << 32U)) & ((1ULL << 40U) - 1ULL)) << 8U;
  output.mipLevels = ((high >> 8U) & 0xFFFU) + 1U;
  output.width = ((high >> 20U) & 0xFFFU) + 1U;
  output.height = (tsharp[2] & 0x3FFFU) + 1U;
  output.format = formatFor((tsharp[2] >> 15U) & 0x3FU, (tsharp[2] >> 20U) & 0x7U);
  if (output.address == 0 || output.width == 0 || output.height == 0) {
    error = "T# descriptor contains an invalid image extent or address";
    return false;
  }
  error.clear();
  return true;
}

std::uint32_t TextureCache::findMemoryType(const std::uint32_t bits,
                                           const VkMemoryPropertyFlags properties) const noexcept {
  VkPhysicalDeviceMemoryProperties memoryProperties{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
  for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
    if ((bits & (1U << index)) != 0U &&
        (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties) return index;
  }
  return std::numeric_limits<std::uint32_t>::max();
}

VkImageView TextureCache::getOrUpload(const std::uint32_t tsharp[8], const VkCommandBuffer uploadCommand,
                                      std::string& error) {
  Decoded decoded{};
  if (!decode(tsharp, decoded, error)) return VK_NULL_HANDLE;
  if (const auto found = entries_.find(decoded.address); found != entries_.end()) return found->second.view;

  VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = decoded.format;
  imageInfo.extent = {decoded.width, decoded.height, 1};
  imageInfo.mipLevels = decoded.mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  Entry entry{};
  entry.address = decoded.address;
  entry.size = static_cast<std::uint64_t>(decoded.width) * decoded.height * 4U;
  if (vkCreateImage(device_, &imageInfo, nullptr, &entry.image) != VK_SUCCESS) {
    error = "vkCreateImage failed for texture";
    return VK_NULL_HANDLE;
  }
  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device_, entry.image, &requirements);
  VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (allocation.memoryTypeIndex == std::numeric_limits<std::uint32_t>::max() ||
      vkAllocateMemory(device_, &allocation, nullptr, &entry.memory) != VK_SUCCESS ||
      vkBindImageMemory(device_, entry.image, entry.memory, 0) != VK_SUCCESS) {
    destroyEntry(entry);
    error = "could not allocate Vulkan texture memory";
    return VK_NULL_HANDLE;
  }
  VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = entry.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = decoded.format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.levelCount = decoded.mipLevels;
  viewInfo.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device_, &viewInfo, nullptr, &entry.view) != VK_SUCCESS) {
    destroyEntry(entry);
    error = "vkCreateImageView failed for texture";
    return VK_NULL_HANDLE;
  }
  (void)uploadCommand;
  const auto view = entry.view;
  entries_.emplace(decoded.address, entry);
  error.clear();
  return view;
}

void TextureCache::destroyEntry(Entry& entry) noexcept {
  if (device_ == VK_NULL_HANDLE) return;
  if (entry.view != VK_NULL_HANDLE) vkDestroyImageView(device_, entry.view, nullptr);
  if (entry.image != VK_NULL_HANDLE) vkDestroyImage(device_, entry.image, nullptr);
  if (entry.memory != VK_NULL_HANDLE) vkFreeMemory(device_, entry.memory, nullptr);
  entry = {};
}

void TextureCache::invalidateRange(const std::uint64_t address, const std::uint64_t size) noexcept {
  if (size == 0 || address > std::numeric_limits<std::uint64_t>::max() - size) return;
  const auto end = address + size;
  for (auto it = entries_.begin(); it != entries_.end();) {
    const auto entryEnd = it->second.address + it->second.size;
    if (it->second.address < end && address < entryEnd) {
      destroyEntry(it->second);
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

void TextureCache::destroy() noexcept {
  for (auto& [address, entry] : entries_) {
    (void)address;
    destroyEntry(entry);
  }
  entries_.clear();
  device_ = VK_NULL_HANDLE;
  physicalDevice_ = VK_NULL_HANDLE;
  allocator_ = nullptr;
}

} // namespace aceps::gpu

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
