/*
 * ContextTracker.h decodes the small, stable subset of GCN context and shader
 * registers needed to construct a Vulkan graphics pipeline.
 */
#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace aceps::gpu {

struct RenderTargetDesc final {
  std::uint64_t baseAddr{0};
  std::uint32_t pitchInBytes{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
};

struct DepthTargetDesc final {
  std::uint64_t baseAddr{0};
  std::uint32_t width{0};
  std::uint32_t height{0};
  VkFormat format{VK_FORMAT_D32_SFLOAT};
};

struct BlendState final {
  bool blendEnable{false};
  VkBlendFactor srcColor{VK_BLEND_FACTOR_ONE};
  VkBlendFactor dstColor{VK_BLEND_FACTOR_ZERO};
  VkBlendOp colorOp{VK_BLEND_OP_ADD};
  VkBlendFactor srcAlpha{VK_BLEND_FACTOR_ONE};
  VkBlendFactor dstAlpha{VK_BLEND_FACTOR_ZERO};
  VkBlendOp alphaOp{VK_BLEND_OP_ADD};
};

struct DepthStencilState final {
  bool depthTestEnable{false};
  bool depthWriteEnable{false};
  VkCompareOp depthCompareOp{VK_COMPARE_OP_LESS_OR_EQUAL};
  bool stencilEnable{false};
};

struct ViewportState final {
  float x{0.0F};
  float y{0.0F};
  float width{0.0F};
  float height{0.0F};
  float minDepth{0.0F};
  float maxDepth{1.0F};
};

class ContextTracker final {
public:
  void setContextReg(std::uint32_t offset, std::uint32_t value) noexcept;
  void setShReg(std::uint32_t offset, std::uint32_t value) noexcept;

  [[nodiscard]] const RenderTargetDesc& renderTarget(int index) const noexcept;
  [[nodiscard]] const DepthTargetDesc& depthTarget() const noexcept;
  [[nodiscard]] const BlendState& blend(int index) const noexcept;
  [[nodiscard]] const DepthStencilState& depthStencil() const noexcept;
  [[nodiscard]] const ViewportState& viewport(int index) const noexcept;
  [[nodiscard]] std::uint64_t vertexShaderAddr() const noexcept;
  [[nodiscard]] std::uint64_t pixelShaderAddr() const noexcept;
  [[nodiscard]] std::uint64_t computeShaderAddr() const noexcept;
  [[nodiscard]] bool dirty() const noexcept;
  void clearDirty() noexcept;

private:
  // PM4 context offsets are absolute register numbers shifted by two; the
  // resulting index is larger than the compact SH register file.
  static constexpr std::size_t kRegisterCount = 0xB000;
  static constexpr std::uint32_t kColorBase0 = 0xA318U;
  static constexpr std::uint32_t kColorPitch0 = 0xA319U;
  static constexpr std::uint32_t kColorSize0 = 0xA31BU;
  static constexpr std::uint32_t kColorInfo0 = 0xA31CU;
  static constexpr std::uint32_t kDepthInfo = 0xA010U;
  static constexpr std::uint32_t kDepthReadBase = 0xA012U;
  static constexpr std::uint32_t kBlend0 = 0xA340U;
  static constexpr std::uint32_t kViewportScaleX = 0xA2A1U;
  static constexpr std::uint32_t kViewportScaleY = kViewportScaleX + 1U;
  static constexpr std::uint32_t kViewportOffsetX = kViewportScaleX + 2U;
  static constexpr std::uint32_t kViewportOffsetY = kViewportScaleX + 3U;
  static constexpr std::uint32_t kViewportMinDepth = kViewportScaleX + 4U;
  static constexpr std::uint32_t kViewportMaxDepth = kViewportScaleX + 5U;

  static VkFormat colorFormat(std::uint32_t dataFormat, std::uint32_t numFormat) noexcept;
  static VkCompareOp compareOp(std::uint32_t value) noexcept;
  static VkBlendFactor blendFactor(std::uint32_t value) noexcept;
  static VkBlendOp blendOp(std::uint32_t value) noexcept;
  static float floatValue(std::uint32_t value) noexcept;
  void decodeColor(std::size_t index) noexcept;
  void decodeViewport(std::size_t index) noexcept;

  std::array<std::uint32_t, kRegisterCount> contextRegs_{};
  std::array<std::uint32_t, kRegisterCount> shRegs_{};
  std::array<RenderTargetDesc, 8> renderTargets_{};
  DepthTargetDesc depthTarget_{};
  std::array<BlendState, 8> blends_{};
  DepthStencilState depthStencil_{};
  std::array<ViewportState, 16> viewports_{};
  bool dirty_{true};
};

} // namespace aceps::gpu
