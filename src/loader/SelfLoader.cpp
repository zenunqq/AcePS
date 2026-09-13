/*
 * SelfLoader.cpp validates ELF64 images and produces an immutable load plan.
 * It does not map or execute code; that separation keeps parsing safe and
 * makes the plan reusable by a future guest-memory loader.
 */
#include "aceps/loader/SelfLoader.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace aceps::loader {
namespace {
#pragma pack(push, 1)
struct Elf64Header {
  std::uint8_t magic[4];
  std::uint8_t classId;
  std::uint8_t data;
  std::uint8_t version;
  std::uint8_t osAbi;
  std::uint8_t abiVersion;
  std::uint8_t padding[7];
  std::uint16_t type;
  std::uint16_t machine;
  std::uint32_t version2;
  std::uint64_t entry;
  std::uint64_t programHeaderOffset;
  std::uint64_t sectionHeaderOffset;
  std::uint32_t flags;
  std::uint16_t headerSize;
  std::uint16_t programHeaderSize;
  std::uint16_t programHeaderCount;
  std::uint16_t sectionHeaderSize;
  std::uint16_t sectionHeaderCount;
  std::uint16_t sectionNameIndex;
};
struct Elf64ProgramHeader {
  std::uint32_t type;
  std::uint32_t flags;
  std::uint64_t offset;
  std::uint64_t virtualAddress;
  std::uint64_t physicalAddress;
  std::uint64_t fileSize;
  std::uint64_t memorySize;
  std::uint64_t alignment;
};
#pragma pack(pop)

constexpr std::uint32_t loadType = 1;
constexpr std::uint32_t executeFlag = 1;
constexpr std::uint32_t writeFlag = 2;
constexpr std::uint32_t readFlag = 4;

bool rangeWithin(std::uint64_t offset, std::uint64_t size, std::size_t total) {
  return offset <= total && size <= static_cast<std::uint64_t>(total) - offset;
}

} // namespace

bool Elf64Loader::inspect(std::span<const std::uint8_t> image,
                          ElfLoadPlan& plan, std::string& error) {
  plan = {};
  if (image.size() < sizeof(Elf64Header)) {
    error = "ELF image is smaller than its header";
    return false;
  }
  Elf64Header header{};
  std::memcpy(&header, image.data(), sizeof(header));
  if (std::memcmp(header.magic, "\x7f" "ELF", 4) != 0 || header.classId != 2 || header.data != 1) {
    error = "image is not a little-endian ELF64 file";
    return false;
  }
  if (header.version != 1 || header.version2 != 1 || header.headerSize < sizeof(Elf64Header)) {
    error = "ELF version or header size is invalid";
    return false;
  }
  if (header.programHeaderSize != sizeof(Elf64ProgramHeader) || header.programHeaderCount == 0) {
    error = "ELF program-header table is invalid";
    return false;
  }
  const auto tableSize = static_cast<std::uint64_t>(header.programHeaderSize) * header.programHeaderCount;
  if (!rangeWithin(header.programHeaderOffset, tableSize, image.size())) {
    error = "ELF program-header table exceeds the image";
    return false;
  }

  plan.machine = header.machine;
  plan.entryPoint = header.entry;
  plan.segments.reserve(header.programHeaderCount);
  for (std::uint16_t index = 0; index < header.programHeaderCount; ++index) {
    const auto offset = static_cast<std::size_t>(header.programHeaderOffset) +
                        static_cast<std::size_t>(index) * sizeof(Elf64ProgramHeader);
    Elf64ProgramHeader program{};
    std::memcpy(&program, image.data() + offset, sizeof(program));
    if (program.type != loadType) continue;
    if (program.fileSize > program.memorySize || !rangeWithin(program.offset, program.fileSize, image.size())) {
      error = "ELF load segment exceeds file or memory bounds";
      plan = {};
      return false;
    }
    if (program.alignment != 0 && (program.alignment & (program.alignment - 1U)) != 0) {
      error = "ELF load segment alignment is not a power of two";
      plan = {};
      return false;
    }
    if (program.virtualAddress > std::numeric_limits<std::uint64_t>::max() - program.memorySize) {
      error = "ELF load segment virtual-address range overflows";
      plan = {};
      return false;
    }
    SegmentFlags flags = SegmentFlags::None;
    if ((program.flags & executeFlag) != 0) flags = static_cast<SegmentFlags>(static_cast<std::uint32_t>(flags) | executeFlag);
    if ((program.flags & writeFlag) != 0) flags = static_cast<SegmentFlags>(static_cast<std::uint32_t>(flags) | writeFlag);
    if ((program.flags & readFlag) != 0) flags = static_cast<SegmentFlags>(static_cast<std::uint32_t>(flags) | readFlag);
    plan.segments.push_back(LoadSegment{program.offset, program.virtualAddress, program.fileSize,
                                        program.memorySize, program.alignment, flags});
  }
  if (plan.segments.empty()) {
    error = "ELF contains no loadable segments";
    plan = {};
    return false;
  }
  error.clear();
  return true;
}

} // namespace aceps::loader
