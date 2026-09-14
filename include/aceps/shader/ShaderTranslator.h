/*
 * ShaderTranslator.h translates the supported PS4 GCN instruction subset into
 * deterministic Vulkan SPIR-V modules without exceptions on the hot path.
 */
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace aceps::shader {

enum class ShaderType : std::uint8_t {
  Auto,
  Vertex,
  Fragment,
  Compute,
};

class ShaderTranslator final {
public:
  [[nodiscard]] static bool translate(std::span<const std::uint32_t> gcnBytecode,
                                      ShaderType hint,
                                      std::vector<std::uint32_t>& spirvOut,
                                      std::string& error);
};

} // namespace aceps::shader
