/*
 * VirtualFileSystem.h maps Orbis-style namespaces to approved host roots.
 * Resolution rejects traversal outside the configured root and never exposes
 * host paths directly to callers.
 */
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace aceps::filesystem {

class VirtualFileSystem final {
public:
  VirtualFileSystem(std::filesystem::path applicationRoot,
                    std::filesystem::path saveRoot);

  [[nodiscard]] std::optional<std::filesystem::path> resolve(std::string_view guestPath,
                                                               std::string& error) const;

private:
  std::filesystem::path applicationRoot_;
  std::filesystem::path saveRoot_;
};

} // namespace aceps::filesystem
