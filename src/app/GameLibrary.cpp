/*
 * GameLibrary.cpp implements deterministic discovery of unpacked game folders
 * and readable unencrypted PKG packages.
 */
#include "aceps/app/GameLibrary.h"

#include "aceps/loader/PkgLoader.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace aceps::app {
namespace {

bool isPkgPath(const std::filesystem::path& path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return extension == ".pkg";
}

GameEntry entryFromPkg(const std::filesystem::path& path, const loader::PkgInfo& info) {
  GameEntry entry;
  entry.root = path;
  entry.sourcePath = path;
  entry.displayName = info.title.empty() ? path.stem().string() : info.title;
  entry.titleId = info.titleId.empty() ? info.contentId : info.titleId;
  entry.version = info.appVersion;
  return entry;
}

} // namespace

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
    if (item.is_regular_file(code) && !code && isPkgPath(item.path())) {
      loader::PkgLoader package;
      std::string packageError;
      if (package.parse(item.path(), packageError)) entries_.push_back(entryFromPkg(item.path(), package.info()));
      code.clear();
      continue;
    }
    if (code || !item.is_directory(code)) {
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
    entry.sourcePath = item.path();
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
  if (code) {
    error = "could not normalize game path: " + code.message();
    return false;
  }

  if (isPkgPath(normalized)) {
    if (!std::filesystem::is_regular_file(normalized, code) || code) {
      error = "selected PKG is not a readable file";
      return false;
    }
    loader::PkgLoader package;
    if (!package.parse(normalized, error)) return false;
    const auto duplicate = std::find_if(entries_.begin(), entries_.end(), [&](const GameEntry& entry) {
      return entry.sourcePath == normalized;
    });
    if (duplicate != entries_.end()) {
      error = "game is already in the library";
      return false;
    }
    entries_.push_back(entryFromPkg(normalized, package.info()));
  } else {
    if (!std::filesystem::is_directory(normalized, code) || code) {
      error = "game path is not a readable directory";
      return false;
    }
    if (!std::filesystem::is_directory(normalized / "sce_sys", code) || code) {
      error = "selected folder does not contain a sce_sys directory";
      return false;
    }
    const auto duplicate = std::find_if(entries_.begin(), entries_.end(), [&](const GameEntry& entry) {
      return entry.sourcePath == normalized;
    });
    if (duplicate != entries_.end()) {
      error = "game is already in the library";
      return false;
    }
    GameEntry entry;
    entry.root = normalized;
    entry.sourcePath = normalized;
    entry.displayName = normalized.filename().string();
    entry.titleId = entry.displayName;
    entries_.push_back(std::move(entry));
  }

  std::sort(entries_.begin(), entries_.end(), [](const GameEntry& left, const GameEntry& right) {
    return left.displayName < right.displayName;
  });
  error.clear();
  return true;
}

const std::vector<GameEntry>& GameLibrary::entries() const noexcept { return entries_; }

const std::filesystem::path& GameLibrary::root() const noexcept { return root_; }

} // namespace aceps::app
