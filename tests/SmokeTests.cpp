/*
 * SmokeTests.cpp exercises configuration persistence, path safety, and the
 * emulator lifecycle without requiring Qt, Vulkan, or game assets.
 */
#include "aceps/config/ApplicationConfig.h"
#include "aceps/config/PlatformPaths.h"
#include "aceps/app/GameLibrary.h"
#include "aceps/core/Emulator.h"
#include "aceps/core/EmulatorError.h"
#include "aceps/filesystem/VirtualFileSystem.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}
} // namespace

int main() {
  const auto temporaryRoot = std::filesystem::temp_directory_path() / "aceps-smoke";
  std::filesystem::remove_all(temporaryRoot);
  std::filesystem::create_directories(temporaryRoot);

  const auto defaults = aceps::config::ApplicationConfig::defaults(temporaryRoot);
  std::string error;
  require(defaults.validate(error), "default configuration must validate");
  require(error.empty(), "valid configuration must clear the error");

  const auto configFile = temporaryRoot / "config.ini";
  require(defaults.save(configFile, error), "configuration must save");
  const auto loaded = aceps::config::ApplicationConfig::load(configFile, error);
  require(loaded.has_value(), "saved configuration must load");
  require(loaded->libraryDirectory == defaults.libraryDirectory, "library path must round-trip");
  require(loaded->graphics.vsync == defaults.graphics.vsync, "graphics settings must round-trip");

  auto invalid = defaults;
  invalid.graphics.resolutionScale = 5;
  require(!invalid.validate(error), "out-of-range scale must be rejected");
  require(!error.empty(), "invalid configuration must explain the error");

  aceps::filesystem::VirtualFileSystem vfs(temporaryRoot / "app", temporaryRoot / "save");
  const auto appFile = vfs.resolve("/app0/sce_sys/param.sfo", error);
  require(appFile.has_value(), "app0 path must resolve");
  require(appFile->filename() == "param.sfo", "resolved app path must retain filename");
  require(!vfs.resolve("/app0/../../escape", error).has_value(), "path traversal must be rejected");
  require(!vfs.resolve("/kernel/config", error).has_value(), "unknown namespace must be rejected");

  const auto gamesRoot = temporaryRoot / "games";
  std::filesystem::create_directories(gamesRoot / "Zeta" / "sce_sys");
  std::filesystem::create_directories(gamesRoot / "Alpha" / "sce_sys");
  std::filesystem::create_directories(gamesRoot / "NotAGame");
  aceps::app::GameLibrary library(gamesRoot);
  require(library.refresh(error), "game library scan must succeed");
  require(library.entries().size() == 2, "only directories with sce_sys are cataloged");
  require(library.entries().front().displayName == "Alpha", "library entries are sorted");

  aceps::core::Emulator emulator(defaults);
  require(emulator.state() == aceps::core::EmulatorState::Created, "emulator starts in Created");
  emulator.initialize();
  require(emulator.isInitialized(), "emulator enters Running");
  require(emulator.initializedSubsystemCount() == 5, "all starter services initialize");
  emulator.initialize();
  require(emulator.initializedSubsystemCount() == 5, "initialize is idempotent");
  emulator.shutdown();
  require(emulator.state() == aceps::core::EmulatorState::Stopped, "emulator enters Stopped");
  require(emulator.initializedSubsystemCount() == 0, "shutdown clears services");

  bool threw = false;
  try {
    aceps::core::Emulator broken(invalid);
    broken.initialize();
  } catch (const aceps::core::EmulatorError&) {
    threw = true;
  }
  require(threw, "invalid configuration must throw a typed emulator error");
  std::filesystem::remove_all(temporaryRoot);
  return 0;
}
