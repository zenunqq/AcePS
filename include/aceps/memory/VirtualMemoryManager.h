/*
 * VirtualMemoryManager.h exposes page-granular guest memory allocation and
 * protection. The implementation owns host mappings and rejects invalid or
 * overlapping releases before they can corrupt emulator state.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace aceps::memory {

enum class Protection : std::uint8_t { None, Read, ReadWrite, ReadExecute, ReadWriteExecute };

class VirtualMemoryManager final {
public:
  explicit VirtualMemoryManager(std::size_t pageSize = 4096);
  ~VirtualMemoryManager();

  VirtualMemoryManager(const VirtualMemoryManager&) = delete;
  VirtualMemoryManager& operator=(const VirtualMemoryManager&) = delete;

  [[nodiscard]] void* allocate(std::size_t size, Protection protection, std::string& error);
  [[nodiscard]] bool protect(void* address, std::size_t size, Protection protection, std::string& error);
  [[nodiscard]] bool release(void* address, std::size_t size, std::string& error);
  [[nodiscard]] std::size_t pageSize() const noexcept;
  [[nodiscard]] std::size_t allocationCount() const noexcept;
  [[nodiscard]] std::size_t allocatedBytes() const noexcept;

private:
  [[nodiscard]] std::size_t roundToPage(std::size_t size) const;
  std::size_t pageSize_;
  std::unordered_map<void*, std::size_t> allocations_;
  std::size_t allocatedBytes_{0};
  mutable std::mutex mutex_;
};

} // namespace aceps::memory
