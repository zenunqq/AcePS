/*
 * GameLibrary.cpp implements conservative first-generation game discovery.
 * A directory is cataloged when it contains a sce_sys folder; metadata parsing
 * is intentionally isolated for a later SFO reader milestone.
 */
#include "aceps/app/GameLibrary.h"

#include <algorithm>
#include <system_error>

namespace aceps::app {

GameLibrary::GameLibrary(std::filesystem::path libraryRoot)
    : root_(std::filesystem::absolute(std::move(libraryRoot)).lexically_normal()) {}

bool GameLibrary::refresh(std::string& error) {
  entries_.clear();
  std::error_code code;
  if (!std::filesystem::exists(root_, code)) {
    if (code) {
      error = "could not inspect library directory: " + code.message();
      return false;
    }
    error.clear();
    return true;
  }
  if (!std::filesystem::is_directory(root_, code)) {
    error = "library path is not a directory: " + root_.string();
    return false;
  }

  for (const auto& item : std::filesystem::directory_iterator(root_, code)) {
    if (code) {
      error = "could not enumerate library directory: " + code.message();
      return false;
    }
    if (!item.is_directory(code) || code) {
      code.clear();
      continue;
    }
    const auto systemDirectory = item.path() / "sce_sys";
    if (!std::filesystem::is_directory(systemDirectory, code) || code) {
      code.clear();
      continue;
    }
    GameEntry entry;
    entry.root = item.path();
    entry.displayName = item.path().filename().string();
    entry.titleId = entry.displayName;
    entries_.push_back(std::move(entry));
  }

  std::sort(entries_.begin(), entries_.end(), [](const GameEntry& left, const GameEntry& right) {
    return left.displayName < right.displayName;
  });
  error.clear();
  return true;
}

bool GameLibrary::addPath(const std::filesystem::path& gamePath, std::string& error) {
  std::error_code code;
  const auto normalized = std::filesystem::absolute(gamePath, code).lexically_normal();
  if (code || !std::filesystem::is_directory(normalized, code)) {
    error = "game path is not a readable directory";
    return false;
  }
  if (!std::filesystem::is_directory(normalized / "sce_sys", code) || code) {
    error = "selected folder does not contain a sce_sys directory";
    return false;
  }
  const auto duplicate = std::find_if(entries_.begin(), entries_.end(), [&](const GameEntry& entry) {
    return entry.root == normalized;
  });
  if (duplicate != entries_.end()) {
    error = "game is already in the library";
    return false;
  }
  entries_.push_back(GameEntry{normalized, normalized.filename().string(), normalized.filename().string()});
  std::sort(entries_.begin(), entries_.end(), [](const GameEntry& left, const GameEntry& right) {
    return left.displayName < right.displayName;
  });
  error.clear();
  return true;
}

const std::vector<GameEntry>& GameLibrary::entries() const noexcept { return entries_; }

const std::filesystem::path& GameLibrary::root() const noexcept { return root_; }

} // namespace aceps::app
