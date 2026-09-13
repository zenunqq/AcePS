/*
 * GameLibrary.h defines the headless game catalog used by the UI and future
 * command-line tooling. Scanning is deterministic and never follows unknown
 * files as executable content.
 */
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace aceps::app {

struct GameEntry final {
  std::filesystem::path root;
  std::string displayName;
  std::string titleId;
};

class GameLibrary final {
public:
  explicit GameLibrary(std::filesystem::path libraryRoot);

  [[nodiscard]] bool refresh(std::string& error);
  [[nodiscard]] bool addPath(const std::filesystem::path& gamePath, std::string& error);
  [[nodiscard]] const std::vector<GameEntry>& entries() const noexcept;
  [[nodiscard]] const std::filesystem::path& root() const noexcept;

private:
  std::filesystem::path root_;
  std::vector<GameEntry> entries_;
};

} // namespace aceps::app
