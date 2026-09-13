/*
 * ElfMapper.cpp implements transactional mapping of validated ELF segments.
 * Segments are writable while initialized, then receive their final host
 * protection after file-backed and zero-filled bytes have been populated.
 */
#include "aceps/loader/ElfMapper.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace aceps::loader {

ElfMapper::~ElfMapper() { unmap(); }

bool ElfMapper::checkedRange(const std::uint64_t offset,
                             const std::uint64_t size,
                             const std::size_t limit) noexcept {
  return offset <= static_cast<std::uint64_t>(limit) &&
         size <= static_cast<std::uint64_t>(limit) - offset;
}

memory::Protection ElfMapper::protectionFor(const SegmentFlags flags) noexcept {
  const auto rawFlags = static_cast<std::uint32_t>(flags);
  if ((rawFlags & static_cast<std::uint32_t>(SegmentFlags::Execute)) != 0U) {
    return memory::Protection::ReadExecute;
  }
  if ((rawFlags & static_cast<std::uint32_t>(SegmentFlags::Write)) != 0U) {
    return memory::Protection::ReadWrite;
  }
  return memory::Protection::Read;
}

bool ElfMapper::map(const std::span<const std::uint8_t> image,
                    const ElfLoadPlan& plan,
                    memory::VirtualMemoryManager& memory,
                    std::string& error) {
  unmap();
  std::scoped_lock lock(mutex_);
  memory_ = &memory;
  entryPoint_ = 0;

  for (const auto& segment : plan.segments) {
    if (segment.memorySize == 0 || segment.fileSize > segment.memorySize) {
      error = "ELF segment has an invalid file or memory size";
      (void)releaseMappings(error);
      return false;
    }
    if (!checkedRange(segment.fileOffset, segment.fileSize, image.size())) {
      error = "ELF segment file range exceeds the image";
      (void)releaseMappings(error);
      return false;
    }
    if (segment.memorySize > std::numeric_limits<std::size_t>::max()) {
      error = "ELF segment memory size exceeds host limits";
      (void)releaseMappings(error);
      return false;
    }

    const auto allocationSize = static_cast<std::size_t>(segment.memorySize);
    void* address = memory.allocate(allocationSize, memory::Protection::ReadWrite, error);
    if (address == nullptr) {
      (void)releaseMappings(error);
      return false;
    }

    auto* destination = static_cast<std::uint8_t*>(address);
    if (segment.fileSize > 0) {
      std::memcpy(destination,
                  image.data() + static_cast<std::size_t>(segment.fileOffset),
                  static_cast<std::size_t>(segment.fileSize));
    }
    if (segment.memorySize > segment.fileSize) {
      std::memset(destination + static_cast<std::size_t>(segment.fileSize), 0,
                  allocationSize - static_cast<std::size_t>(segment.fileSize));
    }

    if (!memory.protect(address, allocationSize, protectionFor(segment.flags), error)) {
      std::string releaseError;
      (void)memory.release(address, allocationSize, releaseError);
      (void)releaseMappings(error);
      return false;
    }

    mappings_.push_back(OwnedMapping{
        ElfMapping{address, segment.virtualAddress, allocationSize},
        allocationSize,
        &memory});
  }

  entryPoint_ = plan.entryPoint;
  error.clear();
  return true;
}

bool ElfMapper::releaseMappings(std::string& error) noexcept {
  bool success = true;
  std::string releaseError;
  for (auto it = mappings_.rbegin(); it != mappings_.rend(); ++it) {
    if (it->memory != nullptr &&
        !it->memory->release(it->mapping.hostAddress, it->allocatedSize, releaseError)) {
      success = false;
      if (error.empty()) error = releaseError;
    }
  }
  mappings_.clear();
  memory_ = nullptr;
  entryPoint_ = 0;
  return success;
}

void ElfMapper::unmap() noexcept {
  std::scoped_lock lock(mutex_);
  std::string ignored;
  (void)releaseMappings(ignored);
}

std::uint64_t ElfMapper::entryPoint() const noexcept {
  std::scoped_lock lock(mutex_);
  return entryPoint_;
}

std::size_t ElfMapper::mappingCount() const noexcept {
  std::scoped_lock lock(mutex_);
  return mappings_.size();
}

std::vector<ElfMapping> ElfMapper::mappings() const {
  std::scoped_lock lock(mutex_);
  std::vector<ElfMapping> result;
  result.reserve(mappings_.size());
  std::transform(mappings_.begin(), mappings_.end(), std::back_inserter(result),
                 [](const OwnedMapping& mapping) { return mapping.mapping; });
  return result;
}

} // namespace aceps::loader
