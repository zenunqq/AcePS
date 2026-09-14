/* FramePresenter.h owns an optional Vulkan surface presentation lifecycle. */
#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace aceps::gpu {

class FramePresenter final {
public:
  FramePresenter() = default;
  ~FramePresenter();
  FramePresenter(const FramePresenter&) = delete;
  FramePresenter& operator=(const FramePresenter&) = delete;

  [[nodiscard]] bool init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
                          VkSurfaceKHR surface, std::uint32_t width, std::uint32_t height,
                          std::uint32_t graphicsFamily, std::uint32_t presentFamily,
                          std::string& error);
  void destroy() noexcept;
  [[nodiscard]] bool beginFrame(VkCommandBuffer& command, std::uint32_t& imageIndex,
                                std::string& error);
  [[nodiscard]] bool present(VkCommandBuffer command, VkSemaphore renderDone,
                             std::string& error);
  [[nodiscard]] VkRenderPass renderPass() const noexcept { return renderPass_; }
  [[nodiscard]] VkFramebuffer framebuffer(std::uint32_t index) const noexcept;
  [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
  [[nodiscard]] std::uint32_t imageCount() const noexcept {
    return static_cast<std::uint32_t>(images_.size());
  }

private:
  [[nodiscard]] bool createSwapchain(std::uint32_t width, std::uint32_t height, std::string& error);
  [[nodiscard]] bool createRenderPass(std::string& error);
  [[nodiscard]] bool createFramebuffers(std::string& error);
  VkInstance instance_{VK_NULL_HANDLE};
  VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkQueue graphicsQueue_{VK_NULL_HANDLE};
  VkQueue presentQueue_{VK_NULL_HANDLE};
  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
  VkRenderPass renderPass_{VK_NULL_HANDLE};
  VkExtent2D extent_{};
  VkFormat format_{VK_FORMAT_B8G8R8A8_UNORM};
  std::uint32_t graphicsFamily_{0};
  std::uint32_t presentFamily_{0};
  std::vector<VkImage> images_;
  std::vector<VkImageView> views_;
  std::vector<VkFramebuffer> framebuffers_;
  std::vector<VkCommandPool> pools_;
  std::vector<VkCommandBuffer> commands_;
  std::vector<VkFence> fences_;
  VkSemaphore imageAvailable_{VK_NULL_HANDLE};
  VkSemaphore renderFinished_{VK_NULL_HANDLE};
  std::uint32_t currentFrame_{0};
  std::uint32_t currentImage_{0};
};

} // namespace aceps::gpu
