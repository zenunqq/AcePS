/*
 * PkgLoader.cpp implements bounds-checked parsing for unencrypted PKG files.
 * It never attempts decryption and reports the key requirement explicitly.
 */
#include "aceps/loader/PkgLoader.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

namespace aceps::loader {
namespace {

constexpr std::uint32_t kPkgMagic = 0x7F434E54U;
constexpr std::size_t kPackageTypeOffset = 4;
constexpr std::size_t kFileTableOffset = 0x10;
constexpr std::size_t kEntryCountOffset = 0x14;
constexpr std::size_t kFlagsOffset = 0x1C;
constexpr std::size_t kContentIdOffset = 0x30;
constexpr std::size_t kContentIdSize = 48;
constexpr std::size_t kFileEntrySize = 32;
constexpr std::uint32_t kEncryptedFlag = 0x80000000U;

constexpr std::uint32_t kSfoMagic = 0x00505346U;
constexpr std::size_t kSfoHeaderSize = 20;
constexpr std::size_t kSfoEntrySize = 16;

template <typename Integer>
bool readInteger(const std::vector<std::uint8_t>& image, std::size_t offset, Integer& value) noexcept {
  if (offset > image.size() || sizeof(Integer) > image.size() - offset) return false;
  std::memcpy(&value, image.data() + offset, sizeof(Integer));
  return true;
}

std::string boundedString(const std::vector<std::uint8_t>& image,
                          std::size_t offset, std::size_t size) {
  const auto end = std::find(image.begin() + static_cast<std::ptrdiff_t>(offset),
                             image.begin() + static_cast<std::ptrdiff_t>(offset + size), 0);
  return std::string(image.begin() + static_cast<std::ptrdiff_t>(offset), end);
}

} // namespace

bool PkgLoader::rangeValid(const std::uint64_t offset, const std::uint64_t size,
                           const std::uint64_t limit) noexcept {
  return offset <= limit && size <= limit - offset;
}

bool PkgLoader::readFile(const std::filesystem::path& path,
                         std::vector<std::uint8_t>& image,
                         std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "could not open PKG: " + path.string();
    return false;
  }
  input.seekg(0, std::ios::end);
  const auto length = input.tellg();
  if (length < 0) {
    error = "could not determine PKG size: " + path.string();
    return false;
  }
  input.seekg(0, std::ios::beg);
  image.resize(static_cast<std::size_t>(length));
  if (!image.empty()) input.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()));
  if (!input && !image.empty()) {
    error = "could not read PKG: " + path.string();
    return false;
  }
  error.clear();
  return true;
}

bool PkgLoader::parse(const std::filesystem::path& path, std::string& error) {
  std::vector<std::uint8_t> image;
  if (!readFile(path, image, error)) return false;
  if (image.size() < kContentIdOffset + kContentIdSize) {
    error = "PKG header is truncated";
    return false;
  }

  std::uint32_t magic = 0;
  std::uint32_t packageType = 0;
  std::uint32_t flags = 0;
  std::uint32_t entryCount = 0;
  std::uint32_t tableOffset = 0;
  if (!readInteger(image, 0, magic) || magic != kPkgMagic ||
      !readInteger(image, kPackageTypeOffset, packageType) ||
      !readInteger(image, kFlagsOffset, flags) ||
      !readInteger(image, kFileTableOffset, tableOffset) ||
      !readInteger(image, kEntryCountOffset, entryCount)) {
    error = "invalid or unsupported PKG header";
    return false;
  }

  PkgInfo parsedInfo;
  parsedInfo.packageType = packageType;
  parsedInfo.encrypted = (flags & kEncryptedFlag) != 0;
  const auto contentEnd = std::find(image.begin() + static_cast<std::ptrdiff_t>(kContentIdOffset),
                                    image.begin() + static_cast<std::ptrdiff_t>(kContentIdOffset + kContentIdSize), 0);
  parsedInfo.contentId.assign(image.begin() + static_cast<std::ptrdiff_t>(kContentIdOffset), contentEnd);
  if (parsedInfo.encrypted) {
    error = "PKG is encrypted; decryption keys are required and no decryption was attempted";
    return false;
  }
  const auto tableSize = static_cast<std::uint64_t>(entryCount) * kFileEntrySize;
  if (!rangeValid(tableOffset, tableSize, image.size())) {
    error = "PKG file table exceeds the package";
    return false;
  }

  std::vector<PkgEntry> parsedEntries;
  parsedEntries.reserve(entryCount);
  for (std::uint32_t index = 0; index < entryCount; ++index) {
    const auto base = static_cast<std::size_t>(tableOffset + static_cast<std::uint64_t>(index) * kFileEntrySize);
    std::uint32_t nameOffset = 0;
    std::uint32_t nameSize = 0;
    std::uint64_t dataOffset = 0;
    std::uint64_t dataSize = 0;
    std::uint32_t entryFlags = 0;
    if (!readInteger(image, base, nameOffset) || !readInteger(image, base + 4, nameSize) ||
        !readInteger(image, base + 8, dataOffset) || !readInteger(image, base + 16, dataSize) ||
        !readInteger(image, base + 24, entryFlags) ||
        !rangeValid(nameOffset, nameSize, image.size()) ||
        !rangeValid(dataOffset, dataSize, image.size())) {
      error = "PKG file entry is outside the package";
      return false;
    }
    parsedEntries.push_back(PkgEntry{boundedString(image, nameOffset, nameSize), dataOffset, dataSize, entryFlags});
  }

  packagePath_ = path;
  info_ = std::move(parsedInfo);
  entries_ = std::move(parsedEntries);
  const auto* sfo = findEntry("sce_sys/param.sfo");
  if (sfo != nullptr && !parseSfo(image, *sfo, error)) return false;
  error.clear();
  return true;
}

bool PkgLoader::parseSfo(const std::vector<std::uint8_t>& image,
                         const PkgEntry& entry,
                         std::string& error) {
  if (entry.dataSize > std::numeric_limits<std::size_t>::max() ||
      !rangeValid(entry.dataOffset, entry.dataSize, image.size())) {
    error = "SFO entry exceeds the package";
    return false;
  }
  const auto base = static_cast<std::size_t>(entry.dataOffset);
  const auto size = static_cast<std::size_t>(entry.dataSize);
  if (size < kSfoHeaderSize) {
    error = "param.sfo is truncated";
    return false;
  }
  std::uint32_t magic = 0;
  std::uint32_t keyOffset = 0;
  std::uint32_t valueOffset = 0;
  std::uint32_t count = 0;
  if (!readInteger(image, base, magic) || magic != kSfoMagic ||
      !readInteger(image, base + 8, keyOffset) || !readInteger(image, base + 12, valueOffset) ||
      !readInteger(image, base + 16, count) || count > (size - kSfoHeaderSize) / kSfoEntrySize) {
    error = "invalid param.sfo header";
    return false;
  }
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto record = base + kSfoHeaderSize + static_cast<std::size_t>(index) * kSfoEntrySize;
    std::uint16_t nameOffset = 0;
    std::uint32_t valueSize = 0;
    std::uint32_t valueDataOffset = 0;
    if (!readInteger(image, record, nameOffset) || !readInteger(image, record + 4, valueSize) ||
        !readInteger(image, record + 12, valueDataOffset) ||
        !rangeValid(static_cast<std::uint64_t>(keyOffset) + nameOffset, 1,
                    static_cast<std::uint64_t>(size)) ||
        !rangeValid(static_cast<std::uint64_t>(valueOffset) + valueDataOffset, valueSize,
                    static_cast<std::uint64_t>(size))) {
      error = "param.sfo record exceeds the file";
      return false;
    }
    const auto nameStart = base + keyOffset + nameOffset;
    const auto valueStart = base + valueOffset + valueDataOffset;
    const auto nameLimit = base + size;
    auto nameEnd = nameStart;
    while (nameEnd < nameLimit && image[nameEnd] != 0) ++nameEnd;
    if (nameEnd == nameLimit) continue;
    const auto valueLength = std::find(image.begin() + static_cast<std::ptrdiff_t>(valueStart),
                                       image.begin() + static_cast<std::ptrdiff_t>(valueStart + valueSize), 0) -
                             (image.begin() + static_cast<std::ptrdiff_t>(valueStart));
    const std::string name(reinterpret_cast<const char*>(image.data() + nameStart), nameEnd - nameStart);
    const std::string value(reinterpret_cast<const char*>(image.data() + valueStart),
                            static_cast<std::size_t>(valueLength));
    if (name == "TITLE") info_.title = value;
    else if (name == "TITLE_ID") info_.titleId = value;
    else if (name == "APP_VER") info_.appVersion = value;
  }
  error.clear();
  return true;
}

bool PkgLoader::extractFile(std::string_view entryName,
                            const std::filesystem::path& outputPath,
                            std::string& error) const {
  const auto* entry = findEntry(entryName);
  if (entry == nullptr) {
    error = "PKG entry is not present: " + std::string(entryName);
    return false;
  }
  std::ifstream input(packagePath_, std::ios::binary);
  if (!input) {
    error = "could not reopen PKG for extraction";
    return false;
  }
  input.seekg(static_cast<std::streamoff>(entry->dataOffset), std::ios::beg);
  std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "could not open extraction output: " + outputPath.string();
    return false;
  }
  std::vector<char> buffer(64 * 1024);
  std::uint64_t remaining = entry->dataSize;
  while (remaining > 0) {
    const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
    input.read(buffer.data(), chunk);
    if (input.gcount() != chunk) {
      error = "PKG entry read failed during extraction";
      return false;
    }
    output.write(buffer.data(), chunk);
    if (!output) {
      error = "PKG entry write failed during extraction";
      return false;
    }
    remaining -= static_cast<std::uint64_t>(chunk);
  }
  error.clear();
  return true;
}

const PkgEntry* PkgLoader::findEntry(std::string_view name) const noexcept {
  const auto found = std::find_if(entries_.begin(), entries_.end(),
                                  [&](const PkgEntry& entry) { return entry.name == name; });
  return found == entries_.end() ? nullptr : &*found;
}

std::size_t PkgLoader::entryCount() const noexcept { return entries_.size(); }

const std::string& PkgLoader::contentId() const noexcept { return info_.contentId; }

const PkgInfo& PkgLoader::info() const noexcept { return info_; }

} // namespace aceps::loader
