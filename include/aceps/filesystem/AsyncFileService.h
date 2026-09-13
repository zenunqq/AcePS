/*
 * AsyncFileService.h provides bounded asynchronous reads for game assets.
 * Requests return futures, preserve file offsets, and keep filesystem work off
 * the emulation thread.
 */
#pragma once

#include "aceps/core/WorkScheduler.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace aceps::filesystem {

struct FileReadResult final {
  std::vector<std::uint8_t> bytes;
  std::string error;
  [[nodiscard]] bool succeeded() const noexcept { return error.empty(); }
};

class AsyncFileService final {
public:
  explicit AsyncFileService(std::filesystem::path root, std::size_t workers = 2);
  [[nodiscard]] std::future<FileReadResult> read(std::filesystem::path relativePath,
                                                  std::uint64_t offset, std::size_t size);
  void shutdown() noexcept;

private:
  std::filesystem::path root_;
  core::WorkScheduler scheduler_;
};

} // namespace aceps::filesystem
