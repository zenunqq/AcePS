/*
 * AsyncFileService.h provides bounded asynchronous reads for game assets.
 * Requests are root-safe and may reuse recently read chunks through a bounded
 * cache, reducing repeated storage latency during streaming workloads.
 */
#pragma once

#include "aceps/core/WorkScheduler.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace aceps::filesystem {

struct FileReadResult final {
  std::vector<std::uint8_t> bytes;
  std::string error;
  [[nodiscard]] bool succeeded() const noexcept { return error.empty(); }
};

class AsyncFileService final {
public:
  explicit AsyncFileService(std::filesystem::path root, std::size_t workers = 2,
                            std::size_t cacheCapacity = 128);
  [[nodiscard]] std::future<FileReadResult> read(std::filesystem::path relativePath,
                                                  std::uint64_t offset, std::size_t size);
  void clearCache() noexcept;
  void shutdown() noexcept;
  [[nodiscard]] std::size_t cacheHits() const noexcept;
  [[nodiscard]] std::size_t cacheEntries() const noexcept;

private:
  struct CacheKey final {
    std::filesystem::path path;
    std::uint64_t offset;
    std::size_t size;
    bool operator==(const CacheKey& other) const noexcept {
      return path == other.path && offset == other.offset && size == other.size;
    }
  };
  struct CacheKeyHash final {
    std::size_t operator()(const CacheKey& key) const noexcept;
  };
  struct CacheEntry final {
    CacheKey key;
    std::vector<std::uint8_t> bytes;
  };

  std::filesystem::path root_;
  core::WorkScheduler scheduler_;
  const std::size_t cacheCapacity_;
  mutable std::mutex cacheMutex_;
  std::list<CacheEntry> cacheLru_;
  std::unordered_map<CacheKey, std::list<CacheEntry>::iterator, CacheKeyHash> cacheIndex_;
  std::size_t cacheHits_{0};
};

} // namespace aceps::filesystem
