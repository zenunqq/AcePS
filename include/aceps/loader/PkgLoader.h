/*
 * PkgLoader.h parses unencrypted PS4 PKG metadata and file tables. Encrypted
 * packages are identified and rejected without attempting key handling.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace aceps::loader {

struct PkgEntry final {
  std::string name;
  std::uint64_t dataOffset{0};
  std::uint64_t dataSize{0};
  std::uint32_t flags{0};
};

struct PkgInfo final {
  std::string contentId;
  std::uint32_t packageType{0};
  bool encrypted{false};
  std::string title;
  std::string titleId;
  std::string appVersion;
};

class PkgLoader final {
public:
  [[nodiscard]] bool parse(const std::filesystem::path& path, std::string& error);
  [[nodiscard]] bool extractFile(std::string_view entryName,
                                 const std::filesystem::path& outputPath,
                                 std::string& error) const;
  [[nodiscard]] const PkgEntry* findEntry(std::string_view name) const noexcept;
  [[nodiscard]] std::size_t entryCount() const noexcept;
  [[nodiscard]] const std::string& contentId() const noexcept;
  [[nodiscard]] const PkgInfo& info() const noexcept;

private:
  [[nodiscard]] bool parseSfo(const std::vector<std::uint8_t>& image,
                              const PkgEntry& entry,
                              std::string& error);
  [[nodiscard]] static bool readFile(const std::filesystem::path& path,
                                     std::vector<std::uint8_t>& image,
                                     std::string& error);
  [[nodiscard]] static bool rangeValid(std::uint64_t offset, std::uint64_t size,
                                       std::uint64_t limit) noexcept;

  std::filesystem::path packagePath_;
  PkgInfo info_;
  std::vector<PkgEntry> entries_;
};

} // namespace aceps::loader
