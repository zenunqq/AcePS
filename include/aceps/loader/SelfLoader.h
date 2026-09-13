/*
 * SelfLoader.h defines the validated ELF64 load-plan boundary. SELF wrapper
 * handling can feed an extracted ELF payload into this parser without mixing
 * cryptographic/container concerns with segment validation.
 */
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace aceps::loader {

enum class SegmentFlags : std::uint32_t { None = 0, Execute = 1, Write = 2, Read = 4 };

struct LoadSegment final {
  std::uint64_t fileOffset;
  std::uint64_t virtualAddress;
  std::uint64_t fileSize;
  std::uint64_t memorySize;
  std::uint64_t alignment;
  SegmentFlags flags;
};

struct ElfLoadPlan final {
  std::uint16_t machine;
  std::uint64_t entryPoint;
  std::vector<LoadSegment> segments;
};

class Elf64Loader final {
public:
  [[nodiscard]] static bool inspect(std::span<const std::uint8_t> image,
                                    ElfLoadPlan& plan, std::string& error);
};

} // namespace aceps::loader
