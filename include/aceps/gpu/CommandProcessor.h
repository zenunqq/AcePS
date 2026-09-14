/*
 * CommandProcessor.h owns the Vulkan device/swapchain and translates the
 * supported PM4 packet subset into Vulkan command-buffer operations.
 */
#pragma once

#include "aceps/gpu/Pm4Parser.h"
#include "aceps/gpu/ContextTracker.h"
#include "aceps/gpu/PipelineCache.h"
#include "aceps/shader/ShaderTranslator.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace aceps::gpu {

struct NativeWindowHandle final {
  void* display{nullptr};
  std::uint64_t window{0};
};

struct RenderState final {
  std::uint64_t vertexBufferAddress{0};
  std::uint32_t vertexStride{0};
  std::uint64_t indexBufferAddress{0};
  VkIndexType indexType{VK_INDEX_TYPE_UINT32};
  VkRect2D scissor{{0, 0}, {0, 0}};
  VkViewport viewport{0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
  bool blendEnabled{false};
};

class CommandProcessor final {
public:
  CommandProcessor() = default;
  ~CommandProcessor();

  CommandProcessor(const CommandProcessor&) = delete;
  CommandProcessor& operator=(const CommandProcessor&) = delete;

  [[nodiscard]] bool initialize(const NativeWindowHandle* windowHandle,
                                std::uint32_t width,
                                std::uint32_t height,
                                std::string& error);
  void shutdown() noexcept;

  [[nodiscard]] bool beginFrame(std::string& error);
  [[nodiscard]] bool endFrame(std::string& error);
  [[nodiscard]] bool clearScreen(float red, float green, float blue,
                                 std::string& error);
  [[nodiscard]] bool submit(std::span<const std::uint32_t> words, std::string& error);

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] const RenderState& renderState() const noexcept;

private:
  [[nodiscard]] bool createInstance(std::string& error);
  [[nodiscard]] bool createSurface(const NativeWindowHandle* windowHandle, std::string& error);
  [[nodiscard]] bool selectPhysicalDevice(std::string& error);
  [[nodiscard]] bool createDevice(std::string& error);
  [[nodiscard]] bool createSwapchain(std::uint32_t width, std::uint32_t height,
                                     std::string& error);
  [[nodiscard]] bool createCommandResources(std::string& error);
  [[nodiscard]] bool createRenderPass(std::string& error);
  [[nodiscard]] bool createFramebuffers(std::string& error);
  [[nodiscard]] bool dispatch(const Pm4PacketView& packet, std::string& error,
                              std::size_t depth = 0);
  [[nodiscard]] bool recreateSwapchain(std::string& error);
  [[nodiscard]] bool checkResult(VkResult result, const char* operation,
                                 std::string& error) const;
  void destroySwapchainResources() noexcept;
  [[nodiscard]] bool translateShaderModule(std::span<const std::uint32_t> bytecode,
                                           shader::ShaderType type,
                                           std::string& error);
  [[nodiscard]] bool bindGraphicsPipeline(std::string& error);
  [[nodiscard]] bool bindComputePipeline(std::string& error);

  mutable std::mutex mutex_;
  VkInstance instance_{VK_NULL_HANDLE};
  VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkQueue graphicsQueue_{VK_NULL_HANDLE};
  VkQueue presentQueue_{VK_NULL_HANDLE};
  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
  VkRenderPass renderPass_{VK_NULL_HANDLE};
  VkCommandPool commandPool_{VK_NULL_HANDLE};
  std::vector<VkImage> swapchainImages_;
  std::vector<VkImageView> swapchainImageViews_;
  std::vector<VkFramebuffer> framebuffers_;
  std::vector<VkCommandBuffer> commandBuffers_;
  VkCommandBuffer activeCommandBuffer_{VK_NULL_HANDLE};
  std::uint32_t graphicsFamily_{0};
  std::uint32_t presentFamily_{0};
  std::uint32_t imageIndex_{0};
  std::uint32_t width_{0};
  std::uint32_t height_{0};
  VkFormat swapchainFormat_{VK_FORMAT_B8G8R8A8_UNORM};
  bool hasSurface_{false};
  bool frameActive_{false};
  bool renderPassActive_{false};
  bool initialized_{false};
  RenderState state_{};
  std::unordered_map<std::uint32_t, std::uint32_t> contextRegisters_;
  std::unordered_map<std::size_t, VkShaderModule> shaderModules_;
  ContextTracker contextTracker_{};
  ShaderCache shaderCache_{};
  PipelineCache pipelineCache_{};
};

} // namespace aceps::gpu
