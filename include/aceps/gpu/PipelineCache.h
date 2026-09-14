/* PipelineCache.h provides deterministic Vulkan graphics-pipeline caching. */
#pragma once

#include "aceps/gpu/ContextTracker.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace aceps::gpu {

class ShaderCache final {
public:
  void set(std::uint64_t address, VkShaderModule module) { modules_[address] = module; }
  [[nodiscard]] VkShaderModule get(std::uint64_t address) const noexcept {
    const auto found = modules_.find(address);
    return found == modules_.end() ? VK_NULL_HANDLE : found->second;
  }
  void clear() noexcept { modules_.clear(); }

private:
  std::unordered_map<std::uint64_t, VkShaderModule> modules_;
};

class PipelineCache final {
public:
  PipelineCache() = default;
  ~PipelineCache();
  PipelineCache(const PipelineCache&) = delete;
  PipelineCache& operator=(const PipelineCache&) = delete;

  [[nodiscard]] bool init(VkDevice device, std::string& error);
  void destroy() noexcept;
  [[nodiscard]] VkPipeline getOrBuild(const ContextTracker& context, VkRenderPass renderPass,
                                      ShaderCache& shaders, std::string& error);
  [[nodiscard]] VkPipeline getOrBuildCompute(std::uint64_t shaderAddress,
                                             ShaderCache& shaders, std::string& error);

private:
  [[nodiscard]] std::uint64_t hashState(const ContextTracker& context,
                                        std::uint64_t vertexHash,
                                        std::uint64_t pixelHash) const noexcept;
  [[nodiscard]] bool buildGraphicsPipeline(const ContextTracker& context, VkRenderPass renderPass,
                                           VkShaderModule vertex, VkShaderModule pixel,
                                           VkPipeline& output, std::string& error);

  VkDevice device_{VK_NULL_HANDLE};
  VkPipelineCache vkCache_{VK_NULL_HANDLE};
  std::unordered_map<std::uint64_t, VkPipeline> pipelines_;
};

} // namespace aceps::gpu
