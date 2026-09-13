/*
 * PlatformPaths.cpp centralizes host directory conventions for Windows,
 * Linux, and macOS while keeping directory creation explicit and testable.
 */
#include "aceps/config/PlatformPaths.h"

#include <cstdlib>

namespace aceps::config {

std::filesystem::path applicationDataDirectory() {
#ifdef _WIN32
  if (const char* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0') {
    return std::filesystem::path(appData) / "AcePS";
  }
  if (const char* userProfile = std::getenv("USERPROFILE"); userProfile != nullptr && *userProfile != '\0') {
    return std::filesystem::path(userProfile) / "AppData" / "Roaming" / "AcePS";
  }
#elif defined(__APPLE__)
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path(home) / "Library" / "Application Support" / "AcePS";
  }
#else
  if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && *xdg != '\0') {
    return std::filesystem::path(xdg) / "AcePS";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path(home) / ".local" / "share" / "AcePS";
  }
#endif
  return std::filesystem::current_path() / ".aceps";
}

bool ensureDirectory(const std::filesystem::path& directory, std::string& error) {
  std::error_code code;
  std::filesystem::create_directories(directory, code);
  if (code) {
    error = "could not create directory '" + directory.string() + "': " + code.message();
    return false;
  }
  error.clear();
  return true;
}

} // namespace aceps::config
