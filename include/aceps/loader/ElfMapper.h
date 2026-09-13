/*
 * ElfMapper.h maps a validated ELF load plan into host-owned guest memory.
 * The mapper owns each allocation until destruction and rolls back partial
 * mappings when construction fails.
 */
#pragma once

#include "aceps/loader/SelfLoader.h"
#include "aceps/memory/VirtualMemoryManager.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <mutex>
#include <vector>

namespace aceps::loader {

struct ElfMapping final {
  void* hostAddress{nullptr};
  std::uint64_t guestAddress{0};
  std::size_t size{0};
};

class ElfMapper final {
public:
  ElfMapper() = default;
  ~ElfMapper();

  ElfMapper(const ElfMapper&) = delete;
  ElfMapper& operator=(const ElfMapper&) = delete;
  ElfMapper(ElfMapper&&) = delete;
  ElfMapper& operator=(ElfMapper&&) = delete;

  [[nodiscard]] bool map(std::span<const std::uint8_t> image,
                         const ElfLoadPlan& plan,
                         memory::VirtualMemoryManager& memory,
                         std::string& error);

  [[nodiscard]] std::uint64_t entryPoint() const noexcept;
  [[nodiscard]] std::size_t mappingCount() const noexcept;
  [[nodiscard]] std::vector<ElfMapping> mappings() const;
  void unmap() noexcept;

private:
  struct OwnedMapping final {
    ElfMapping mapping;
    std::size_t allocatedSize{0};
    memory::VirtualMemoryManager* memory{nullptr};
  };

  [[nodiscard]] static bool checkedRange(std::uint64_t offset,
                                         std::uint64_t size,
                                         std::size_t limit) noexcept;
  [[nodiscard]] static memory::Protection protectionFor(SegmentFlags flags) noexcept;
  [[nodiscard]] bool releaseMappings(std::string& error) noexcept;

  std::vector<OwnedMapping> mappings_;
  std::uint64_t entryPoint_{0};
  memory::VirtualMemoryManager* memory_{nullptr};
  mutable std::mutex mutex_;
};

} // namespace aceps::loader
