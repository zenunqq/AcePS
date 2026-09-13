/*
 * VirtualMemoryManager.cpp provides the first real host-memory backend for
 * AcePS. Mappings are page-aligned, tracked, and released deterministically.
 */
#include "aceps/memory/VirtualMemoryManager.h"

#include <limits>
#include <cerrno>
#include <cstring>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace aceps::memory {
namespace {

#ifdef _WIN32
DWORD nativeProtection(Protection protection) {
  switch (protection) {
  case Protection::None: return PAGE_NOACCESS;
  case Protection::Read: return PAGE_READONLY;
  case Protection::ReadWrite: return PAGE_READWRITE;
  case Protection::ReadExecute: return PAGE_EXECUTE_READ;
  case Protection::ReadWriteExecute: return PAGE_EXECUTE_READWRITE;
  }
  return PAGE_NOACCESS;
}
#else
int nativeProtection(Protection protection) {
  switch (protection) {
  case Protection::None: return PROT_NONE;
  case Protection::Read: return PROT_READ;
  case Protection::ReadWrite: return PROT_READ | PROT_WRITE;
  case Protection::ReadExecute: return PROT_READ | PROT_EXEC;
  case Protection::ReadWriteExecute: return PROT_READ | PROT_WRITE | PROT_EXEC;
  }
  return PROT_NONE;
}
#endif

} // namespace

VirtualMemoryManager::VirtualMemoryManager(std::size_t pageSize) : pageSize_(pageSize) {
  if (pageSize_ == 0 || (pageSize_ & (pageSize_ - 1)) != 0) pageSize_ = 4096;
}

VirtualMemoryManager::~VirtualMemoryManager() {
  std::scoped_lock lock(mutex_);
  for (const auto& [address, size] : allocations_) {
#ifdef _WIN32
    VirtualFree(address, 0, MEM_RELEASE);
#else
    munmap(address, size);
#endif
  }
  allocations_.clear();
}

std::size_t VirtualMemoryManager::roundToPage(std::size_t size) const {
  if (size == 0 || size > std::numeric_limits<std::size_t>::max() - (pageSize_ - 1)) return 0;
  return ((size + pageSize_ - 1) / pageSize_) * pageSize_;
}

void* VirtualMemoryManager::allocate(std::size_t size, Protection protection, std::string& error) {
  const auto rounded = roundToPage(size);
  if (rounded == 0) {
    error = "allocation size is zero or overflows page rounding";
    return nullptr;
  }
#ifdef _WIN32
  void* address = VirtualAlloc(nullptr, rounded, MEM_RESERVE | MEM_COMMIT, nativeProtection(protection));
  if (address == nullptr) error = "VirtualAlloc failed";
#else
  void* address = mmap(nullptr, rounded, nativeProtection(protection), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (address == MAP_FAILED) {
    address = nullptr;
    error = "mmap failed: " + std::string(std::strerror(errno));
  }
#endif
  if (address == nullptr) return nullptr;
  std::scoped_lock lock(mutex_);
  allocations_.emplace(address, rounded);
  allocatedBytes_ += rounded;
  error.clear();
  return address;
}

bool VirtualMemoryManager::protect(void* address, std::size_t size, Protection protection, std::string& error) {
  std::scoped_lock lock(mutex_);
  const auto found = allocations_.find(address);
  const auto rounded = roundToPage(size);
  if (found == allocations_.end() || rounded == 0 || rounded > found->second) {
    error = "protection range is not an owned allocation";
    return false;
  }
#ifdef _WIN32
  DWORD oldProtection = 0;
  if (VirtualProtect(address, rounded, nativeProtection(protection), &oldProtection) == 0) {
    error = "VirtualProtect failed";
    return false;
  }
#else
  if (mprotect(address, rounded, nativeProtection(protection)) != 0) {
    error = "mprotect failed: " + std::string(std::strerror(errno));
    return false;
  }
#endif
  error.clear();
  return true;
}

bool VirtualMemoryManager::release(void* address, std::size_t size, std::string& error) {
  std::scoped_lock lock(mutex_);
  const auto found = allocations_.find(address);
  const auto rounded = roundToPage(size);
  if (found == allocations_.end() || rounded != found->second) {
    error = "release must match an owned allocation exactly";
    return false;
  }
#ifdef _WIN32
  if (VirtualFree(address, 0, MEM_RELEASE) == 0) {
    error = "VirtualFree failed";
    return false;
  }
#else
  if (munmap(address, found->second) != 0) {
    error = "munmap failed: " + std::string(std::strerror(errno));
    return false;
  }
#endif
  allocations_.erase(found);
  allocatedBytes_ -= rounded;
  error.clear();
  return true;
}

std::size_t VirtualMemoryManager::pageSize() const noexcept { return pageSize_; }
std::size_t VirtualMemoryManager::allocationCount() const noexcept {
  std::scoped_lock lock(mutex_);
  return allocations_.size();
}
std::size_t VirtualMemoryManager::allocatedBytes() const noexcept {
  std::scoped_lock lock(mutex_);
  return allocatedBytes_;
}

} // namespace aceps::memory
