/*
 * VirtualFileSystem.cpp implements only path resolution at this stage. It
 * provides a real security boundary before file-backed HLE services are added.
 */
#include "aceps/filesystem/VirtualFileSystem.h"

#include <string>

namespace aceps::filesystem {
namespace {

bool withinRoot(const std::filesystem::path& candidate, const std::filesystem::path& root) {
  auto candidateIt = candidate.begin();
  auto rootIt = root.begin();
  for (; rootIt != root.end(); ++rootIt, ++candidateIt) {
    if (candidateIt == candidate.end() || *candidateIt != *rootIt) return false;
  }
  return true;
}

} // namespace

VirtualFileSystem::VirtualFileSystem(std::filesystem::path applicationRoot,
                                     std::filesystem::path saveRoot)
    : applicationRoot_(std::filesystem::absolute(std::move(applicationRoot)).lexically_normal()),
      saveRoot_(std::filesystem::absolute(std::move(saveRoot)).lexically_normal()) {}

std::optional<std::filesystem::path> VirtualFileSystem::resolve(std::string_view guestPath,
                                                                  std::string& error) const {
  const std::filesystem::path requested{std::string(guestPath)};
  const auto normalized = requested.lexically_normal();
  std::filesystem::path root;
  std::filesystem::path relative;
  if (normalized == "/app0" || normalized.string().starts_with("/app0/")) {
    root = applicationRoot_;
    relative = normalized.lexically_relative("/app0");
  } else if (normalized == "/savedata" || normalized.string().starts_with("/savedata/")) {
    root = saveRoot_;
    relative = normalized.lexically_relative("/savedata");
  } else {
    error = "unsupported guest path namespace: " + std::string(guestPath);
    return std::nullopt;
  }

  const auto resolved = (root / relative).lexically_normal();
  if (!withinRoot(resolved, root)) {
    error = "guest path escapes its namespace root";
    return std::nullopt;
  }
  error.clear();
  return resolved;
}

} // namespace aceps::filesystem
