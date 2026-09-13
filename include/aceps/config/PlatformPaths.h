/*
 * PlatformPaths.h exposes host-specific application directories without
 * leaking environment-variable or OS naming details into emulator code.
 */
#pragma once

#include <filesystem>
#include <string>

namespace aceps::config {

[[nodiscard]] std::filesystem::path applicationDataDirectory();
[[nodiscard]] bool ensureDirectory(const std::filesystem::path& directory, std::string& error);

} // namespace aceps::config
