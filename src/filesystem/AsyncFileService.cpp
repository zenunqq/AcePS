/*
 * AsyncFileService.cpp implements asynchronous bounded reads. Requests are
 * resolved under a fixed root and reject traversal before opening a file.
 */
#include "aceps/filesystem/AsyncFileService.h"

#include <fstream>
#include <limits>

namespace aceps::filesystem {

AsyncFileService::AsyncFileService(std::filesystem::path root, std::size_t workers)
    : root_(std::filesystem::absolute(std::move(root)).lexically_normal()), scheduler_(workers, 256) {}

std::future<FileReadResult> AsyncFileService::read(std::filesystem::path relativePath,
                                                    std::uint64_t offset, std::size_t size) {
  const auto root = root_;
  return scheduler_.submit([root, relativePath = std::move(relativePath), offset, size]() {
    FileReadResult result;
    const auto normalized = (root / relativePath).lexically_normal();
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
    if (!input && !input.eof()) result.error = "file read failed";
    return result;
  });
}

void AsyncFileService::shutdown() noexcept { scheduler_.shutdown(); }

} // namespace aceps::filesystem
