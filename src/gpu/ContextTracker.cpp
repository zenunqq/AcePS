#include "aceps/gpu/ContextTracker.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include <bit>
#include <cstddef>

namespace aceps::gpu {
namespace {
constexpr std::uint32_t kDepthStencilInfo = 0xA011U;
constexpr std::uint32_t kViewportBase = 0xA2A1U;
constexpr std::uint32_t kPaScScreenScissorTl = 0xA200U;
}

float ContextTracker::floatValue(const std::uint32_t value) noexcept { return std::bit_cast<float>(value); }

VkFormat ContextTracker::colorFormat(const std::uint32_t dataFormat, const std::uint32_t numFormat) noexcept {
  switch (dataFormat) {
  case 0x08U: return numFormat == 0U ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_B8G8R8A8_SRGB;
  case 0x0AU: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
  case 0x0BU: return VK_FORMAT_BC2_UNORM_BLOCK;
  case 0x0CU: return VK_FORMAT_BC3_UNORM_BLOCK;
  case 0x1AU: return numFormat == 4U ? VK_FORMAT_R8G8B8A8_UINT : VK_FORMAT_R8G8B8A8_UNORM;
  case 0x35U: return VK_FORMAT_R16G16B16A16_SFLOAT;
  case 0x39U: return VK_FORMAT_R32G32B32A32_SFLOAT;
  default: return VK_FORMAT_R8G8B8A8_UNORM;
  }
}

VkCompareOp ContextTracker::compareOp(const std::uint32_t value) noexcept {
  return value <= static_cast<std::uint32_t>(VK_COMPARE_OP_ALWAYS) ? static_cast<VkCompareOp>(value)
                                                                     : VK_COMPARE_OP_LESS_OR_EQUAL;
}

VkBlendFactor ContextTracker::blendFactor(const std::uint32_t value) noexcept {
  switch (value & 0x1FU) {
  case 0: return VK_BLEND_FACTOR_ZERO;
  case 1: return VK_BLEND_FACTOR_ONE;
  case 2: return VK_BLEND_FACTOR_SRC_COLOR;
  case 3: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
  case 4: return VK_BLEND_FACTOR_SRC_ALPHA;
  case 5: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  case 6: return VK_BLEND_FACTOR_DST_ALPHA;
  case 7: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  case 8: return VK_BLEND_FACTOR_DST_COLOR;
  case 9: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
  default: return VK_BLEND_FACTOR_ONE;
  }
}

VkBlendOp ContextTracker::blendOp(const std::uint32_t value) noexcept {
  return value <= static_cast<std::uint32_t>(VK_BLEND_OP_MAX_ENUM) && value <= 4U
             ? static_cast<VkBlendOp>(value) : VK_BLEND_OP_ADD;
}

void ContextTracker::decodeColor(const std::size_t index) noexcept {
  if (index >= renderTargets_.size()) return;
  const auto base = index == 0 ? kColorBase0 : kColorBase0 + static_cast<std::uint32_t>(index) * 8U;
  const auto pitch = index == 0 ? kColorPitch0 : base + 1U;
  const auto size = index == 0 ? kColorSize0 : base + 3U;
  const auto info = index == 0 ? contextRegs_[kColorInfo0] : contextRegs_[base + 4U];
  auto& target = renderTargets_[index];
  target.baseAddr = static_cast<std::uint64_t>(contextRegs_[base]) << 8U;
  target.pitchInBytes = (contextRegs_[pitch] + 1U) * 256U;
  target.width = ((contextRegs_[size] >> 16U) & 0x3FFFU) + 1U;
  target.height = (contextRegs_[size] & 0x3FFFU) + 1U;
  target.format = colorFormat((info >> 2U) & 0x3FU, (info >> 8U) & 0x3U);
}

void ContextTracker::decodeViewport(const std::size_t index) noexcept {
  if (index >= viewports_.size()) return;
  const auto base = kViewportBase + static_cast<std::uint32_t>(index) * 6U;
  auto& viewport = viewports_[index];
  viewport.width = floatValue(contextRegs_[base]);
  viewport.height = floatValue(contextRegs_[base + 1U]);
  viewport.x = floatValue(contextRegs_[base + 2U]);
  viewport.y = floatValue(contextRegs_[base + 3U]);
  viewport.minDepth = floatValue(contextRegs_[base + 4U]);
  viewport.maxDepth = floatValue(contextRegs_[base + 5U]);
  if (viewport.maxDepth == 0.0F) viewport.maxDepth = 1.0F;
}

void ContextTracker::setContextReg(const std::uint32_t offset, const std::uint32_t value) noexcept {
  if (offset >= contextRegs_.size()) return;
  contextRegs_[offset] = value;
  dirty_ = true;
  if (offset == kColorBase0 || offset == kColorPitch0 || offset == kColorSize0 || offset == kColorInfo0) decodeColor(0);
  if (offset >= kViewportBase && offset < kViewportBase + viewports_.size() * 6U) {
    decodeViewport((offset - kViewportBase) / 6U);
  }
  if (offset == kDepthInfo || offset == kDepthStencilInfo || offset == kDepthReadBase) {
    depthTarget_.baseAddr = static_cast<std::uint64_t>(contextRegs_[kDepthReadBase]) << 8U;
    depthTarget_.format = ((contextRegs_[kDepthInfo] & 0xFU) == 0U) ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_D24_UNORM_S8_UINT;
    depthStencil_.stencilEnable = (contextRegs_[kDepthStencilInfo] & 0xFU) != 0U;
    depthStencil_.depthTestEnable = (contextRegs_[kDepthInfo] & 0x1U) != 0U;
    depthStencil_.depthWriteEnable = (contextRegs_[kDepthInfo] & 0x2U) != 0U;
    depthStencil_.depthCompareOp = compareOp((contextRegs_[kDepthInfo] >> 4U) & 0x7U);
  }
  if (offset >= kBlend0 && offset < kBlend0 + 8U * 2U) {
    const auto index = (offset - kBlend0) / 2U;
    const auto control = contextRegs_[kBlend0 + index * 2U];
    auto& blend = blends_[index];
    blend.blendEnable = (control & 0x1U) != 0U;
    blend.srcColor = blendFactor(control >> 4U);
    blend.dstColor = blendFactor(control >> 8U);
    blend.colorOp = blendOp(control >> 12U);
    blend.srcAlpha = blendFactor(control >> 16U);
    blend.dstAlpha = blendFactor(control >> 20U);
    blend.alphaOp = blendOp(control >> 24U);
  }
  if (offset == kPaScScreenScissorTl) {
    const auto width = ((value >> 16U) & 0xFFFFU);
    const auto height = value & 0xFFFFU;
    if (width != 0U) viewports_[0].width = static_cast<float>(width);
    if (height != 0U) viewports_[0].height = static_cast<float>(height);
  }
}

void ContextTracker::setShReg(const std::uint32_t offset, const std::uint32_t value) noexcept {
  if (offset >= shRegs_.size()) return;
  shRegs_[offset] = value;
  dirty_ = true;
}

const RenderTargetDesc& ContextTracker::renderTarget(const int index) const noexcept {
  static const RenderTargetDesc empty{};
  return index >= 0 && index < static_cast<int>(renderTargets_.size())
             ? renderTargets_[static_cast<std::size_t>(index)] : empty;
}
const DepthTargetDesc& ContextTracker::depthTarget() const noexcept { return depthTarget_; }
const BlendState& ContextTracker::blend(const int index) const noexcept {
  static const BlendState empty{};
  return index >= 0 && index < static_cast<int>(blends_.size())
             ? blends_[static_cast<std::size_t>(index)] : empty;
}
const DepthStencilState& ContextTracker::depthStencil() const noexcept { return depthStencil_; }
const ViewportState& ContextTracker::viewport(const int index) const noexcept {
  static const ViewportState empty{};
  return index >= 0 && index < static_cast<int>(viewports_.size())
             ? viewports_[static_cast<std::size_t>(index)] : empty;
}
std::uint64_t ContextTracker::vertexShaderAddr() const noexcept {
  return static_cast<std::uint64_t>(shRegs_[0x2C8U]) | (static_cast<std::uint64_t>(shRegs_[0x2C9U]) << 32U);
}
std::uint64_t ContextTracker::pixelShaderAddr() const noexcept {
  return static_cast<std::uint64_t>(shRegs_[0x2CAU]) | (static_cast<std::uint64_t>(shRegs_[0x2CBU]) << 32U);
}
std::uint64_t ContextTracker::computeShaderAddr() const noexcept {
  return static_cast<std::uint64_t>(shRegs_[0x2CCU]) | (static_cast<std::uint64_t>(shRegs_[0x2CDU]) << 32U);
}
bool ContextTracker::dirty() const noexcept { return dirty_; }
void ContextTracker::clearDirty() noexcept { dirty_ = false; }

} // namespace aceps::gpu

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
