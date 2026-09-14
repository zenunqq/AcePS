#include "aceps/gpu/PipelineCache.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include <array>
#include <functional>

namespace aceps::gpu {
namespace {
void appendHash(std::size_t& hash, const std::uint64_t value) noexcept {
  hash ^= static_cast<std::size_t>(value) + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) +
          (hash << 6U) + (hash >> 2U);
}
}

PipelineCache::~PipelineCache() { destroy(); }

bool PipelineCache::init(const VkDevice device, std::string& error) {
  destroy();
  if (device == VK_NULL_HANDLE) {
    error = "pipeline cache requires a valid Vulkan device";
    return false;
  }
  device_ = device;
  VkPipelineCacheCreateInfo createInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
  if (vkCreatePipelineCache(device_, &createInfo, nullptr, &vkCache_) != VK_SUCCESS) {
    device_ = VK_NULL_HANDLE;
    error = "vkCreatePipelineCache failed";
    return false;
  }
  error.clear();
  return true;
}

void PipelineCache::destroy() noexcept {
  if (device_ != VK_NULL_HANDLE) {
    for (const auto& [key, pipeline] : pipelines_) {
      (void)key;
      if (pipeline != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline, nullptr);
    }
    pipelines_.clear();
    if (vkCache_ != VK_NULL_HANDLE) vkDestroyPipelineCache(device_, vkCache_, nullptr);
  }
  vkCache_ = VK_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
}

std::uint64_t PipelineCache::hashState(const ContextTracker& context, const std::uint64_t vertexHash,
                                       const std::uint64_t pixelHash) const noexcept {
  std::size_t hash = 0;
  appendHash(hash, vertexHash);
  appendHash(hash, pixelHash);
  appendHash(hash, context.renderTarget(0).format);
  appendHash(hash, context.depthStencil().depthTestEnable);
  appendHash(hash, context.depthStencil().depthWriteEnable);
  appendHash(hash, context.blend(0).blendEnable);
  return static_cast<std::uint64_t>(hash);
}

bool PipelineCache::buildGraphicsPipeline(const ContextTracker& context, const VkRenderPass renderPass,
                                           const VkShaderModule vertex, const VkShaderModule pixel,
                                           VkPipeline& output, std::string& error) {
  if (vertex == VK_NULL_HANDLE || pixel == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE) {
    error = "graphics pipeline requires vertex shader, pixel shader, and render pass";
    return false;
  }
  const VkPipelineShaderStageCreateInfo stages[] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertex, "main", nullptr},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, pixel, "main", nullptr}};
  VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0F;
  VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  const auto& depth = context.depthStencil();
  VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depthState.depthTestEnable = depth.depthTestEnable;
  depthState.depthWriteEnable = depth.depthWriteEnable;
  depthState.depthCompareOp = depth.depthCompareOp;
  depthState.stencilTestEnable = depth.stencilEnable;
  const auto& blend = context.blend(0);
  VkPipelineColorBlendAttachmentState attachment{};
  attachment.blendEnable = blend.blendEnable;
  attachment.srcColorBlendFactor = blend.srcColor;
  attachment.dstColorBlendFactor = blend.dstColor;
  attachment.colorBlendOp = blend.colorOp;
  attachment.srcAlphaBlendFactor = blend.srcAlpha;
  attachment.dstAlphaBlendFactor = blend.dstAlpha;
  attachment.alphaBlendOp = blend.alphaOp;
  attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blendState.attachmentCount = 1;
  blendState.pAttachments = &attachment;
  const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamicStates;
  VkGraphicsPipelineCreateInfo createInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  createInfo.stageCount = 2;
  createInfo.pStages = stages;
  createInfo.pVertexInputState = &vertexInput;
  createInfo.pInputAssemblyState = &assembly;
  createInfo.pViewportState = &viewport;
  createInfo.pRasterizationState = &raster;
  createInfo.pMultisampleState = &multisample;
  createInfo.pDepthStencilState = &depthState;
  createInfo.pColorBlendState = &blendState;
  createInfo.pDynamicState = &dynamic;
  createInfo.renderPass = renderPass;
  createInfo.subpass = 0;
  if (vkCreateGraphicsPipelines(device_, vkCache_, 1, &createInfo, nullptr, &output) != VK_SUCCESS) {
    error = "vkCreateGraphicsPipelines failed";
    return false;
  }
  return true;
}

VkPipeline PipelineCache::getOrBuild(const ContextTracker& context, const VkRenderPass renderPass,
                                     ShaderCache& shaders, std::string& error) {
  const auto vertex = shaders.get(context.vertexShaderAddr());
  const auto pixel = shaders.get(context.pixelShaderAddr());
  const auto key = hashState(context, reinterpret_cast<std::uint64_t>(vertex), reinterpret_cast<std::uint64_t>(pixel));
  if (const auto found = pipelines_.find(key); found != pipelines_.end()) return found->second;
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (!buildGraphicsPipeline(context, renderPass, vertex, pixel, pipeline, error)) return VK_NULL_HANDLE;
  pipelines_.emplace(key, pipeline);
  error.clear();
  return pipeline;
}

VkPipeline PipelineCache::getOrBuildCompute(const std::uint64_t shaderAddress, ShaderCache& shaders,
                                            std::string& error) {
  const auto shader = shaders.get(shaderAddress);
  if (shader == VK_NULL_HANDLE) {
    error = "compute shader is not present in shader cache";
    return VK_NULL_HANDLE;
  }
  const auto key = shaderAddress ^ 0xC0FFEE77ULL;
  if (const auto found = pipelines_.find(key); found != pipelines_.end()) return found->second;
  VkComputePipelineCreateInfo createInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  createInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_COMPUTE_BIT, shader, "main", nullptr};
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (vkCreateComputePipelines(device_, vkCache_, 1, &createInfo, nullptr, &pipeline) != VK_SUCCESS) {
    error = "vkCreateComputePipelines failed";
    return VK_NULL_HANDLE;
  }
  pipelines_.emplace(key, pipeline);
  error.clear();
  return pipeline;
}

} // namespace aceps::gpu

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
