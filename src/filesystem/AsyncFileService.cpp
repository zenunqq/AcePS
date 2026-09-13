/*
 * AsyncFileService.cpp implements asynchronous bounded reads and a small LRU
 * cache. Cache state is protected separately from the worker scheduler so
 * storage work never holds the cache lock.
 */
#include "aceps/filesystem/AsyncFileService.h"

#include <fstream>

namespace aceps::filesystem {

std::size_t AsyncFileService::CacheKeyHash::operator()(const CacheKey& key) const noexcept {
  const auto pathHash = std::filesystem::hash_value(key.path);
  const auto offsetHash = std::hash<std::uint64_t>{}(key.offset);
  const auto sizeHash = std::hash<std::size_t>{}(key.size);
  return pathHash ^ (offsetHash + 0x9e3779b97f4a7c15ULL + (pathHash << 6U) + (pathHash >> 2U)) ^ sizeHash;
}

AsyncFileService::AsyncFileService(std::filesystem::path root, std::size_t workers,
                                   std::size_t cacheCapacity)
    : root_(std::filesystem::absolute(std::move(root)).lexically_normal()),
      scheduler_(workers, 256), cacheCapacity_(cacheCapacity) {}

std::future<FileReadResult> AsyncFileService::read(std::filesystem::path relativePath,
                                                    std::uint64_t offset, std::size_t size) {
  const auto root = root_;
  const auto keyPath = relativePath.lexically_normal();
  {
    std::scoped_lock lock(cacheMutex_);
    const CacheKey key{keyPath, offset, size};
    const auto found = cacheIndex_.find(key);
    if (found != cacheIndex_.end()) {
      ++cacheHits_;
      cacheLru_.splice(cacheLru_.begin(), cacheLru_, found->second);
      const auto bytes = cacheLru_.front().bytes;
      return std::async(std::launch::deferred, [bytes = std::move(bytes)]() {
        return FileReadResult{bytes, {}};
      });
    }
  }
  return scheduler_.submit([this, root, keyPath, offset, size]() {
    FileReadResult result;
    const auto normalized = (root / keyPath).lexically_normal();
    auto rootIt = root.begin();
    auto pathIt = normalized.begin();
    for (; rootIt != root.end(); ++rootIt, ++pathIt) {
      if (pathIt == normalized.end() || *rootIt != *pathIt) {
        result.error = "file path escapes service root";
        return result;
      }
    }
    std::ifstream input(normalized, std::ios::binary);
    if (!input) {
      result.error = "could not open requested file";
      return result;
    }
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || offset > static_cast<std::uint64_t>(length)) {
      result.error = "read offset exceeds file size";
      return result;
    }
    const auto available = static_cast<std::uint64_t>(length) - offset;
    if (static_cast<std::uint64_t>(size) > available) {
      result.error = "read range exceeds file size";
      return result;
    }
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    result.bytes.resize(size);
    if (size > 0) input.read(reinterpret_cast<char*>(result.bytes.data()), static_cast<std::streamsize>(size));
    if (!input && !input.eof()) {
      result.error = "file read failed";
      return result;
    }
    if (cacheCapacity_ > 0) {
      std::scoped_lock lock(cacheMutex_);
      CacheKey key{keyPath, offset, size};
      if (const auto existing = cacheIndex_.find(key); existing != cacheIndex_.end()) {
        cacheLru_.erase(existing->second);
        cacheIndex_.erase(existing);
      }
      cacheLru_.push_front(CacheEntry{key, result.bytes});
      cacheIndex_[cacheLru_.front().key] = cacheLru_.begin();
      while (cacheLru_.size() > cacheCapacity_) {
        cacheIndex_.erase(cacheLru_.back().key);
        cacheLru_.pop_back();
      }
    }
    return result;
  });
}

void AsyncFileService::clearCache() noexcept {
  std::scoped_lock lock(cacheMutex_);
  cacheIndex_.clear();
  cacheLru_.clear();
}

void AsyncFileService::shutdown() noexcept { scheduler_.shutdown(); }

std::size_t AsyncFileService::cacheHits() const noexcept {
  std::scoped_lock lock(cacheMutex_);
  return cacheHits_;
}

std::size_t AsyncFileService::cacheEntries() const noexcept {
  std::scoped_lock lock(cacheMutex_);
  return cacheLru_.size();
}

} // namespace aceps::filesystem
