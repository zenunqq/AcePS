/*
 * CompatibilityBenchmark.cpp measures async asset reads and GPU queue fences
 * across sequential and concurrent workloads. It is deterministic, dependency-
 * free, and intended for local regression tracking rather than certification.
 */
#include "aceps/filesystem/AsyncFileService.h"
#include "aceps/gpu/GpuQueue.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

double milliseconds(Clock::duration duration) {
  return std::chrono::duration<double, std::milli>(duration).count();
}

void writeAsset(const std::filesystem::path& path, std::size_t size, std::uint8_t seed) {
  std::ofstream output(path, std::ios::binary);
  std::vector<std::uint8_t> bytes(size);
  for (std::size_t index = 0; index < size; ++index) bytes[index] = static_cast<std::uint8_t>(seed + index);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

struct BenchmarkResult final {
  std::string name;
  std::size_t operations;
  double elapsedMs;
  double operationsPerSecond;
};

void print(const BenchmarkResult& result) {
  std::cout << std::left << std::setw(34) << result.name << " "
            << std::right << std::setw(10) << result.operations << " ops  "
            << std::setw(12) << std::fixed << std::setprecision(3) << result.elapsedMs << " ms  "
            << std::setw(14) << std::setprecision(0) << result.operationsPerSecond << " ops/s\n";
}
} // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / "aceps-compat-benchmark";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  writeAsset(root / "asset-a.bin", 64U * 1024U, 3);
  writeAsset(root / "asset-b.bin", 64U * 1024U, 17);

  constexpr std::size_t reads = 256;
  constexpr std::size_t readSize = 4096;
  aceps::filesystem::AsyncFileService files(root, 4);
  std::vector<std::future<aceps::filesystem::FileReadResult>> futures;
  futures.reserve(reads);

  auto start = Clock::now();
  for (std::size_t index = 0; index < reads; ++index) {
    futures.push_back(files.read((index % 2U == 0U) ? "asset-a.bin" : "asset-b.bin",
                                 (index * readSize) % (64U * 1024U - readSize), readSize));
  }
  std::size_t successfulReads = 0;
  for (auto& future : futures) successfulReads += future.get().succeeded() ? 1U : 0U;
  auto elapsed = Clock::now() - start;
  print({"async file concurrent reads", successfulReads, milliseconds(elapsed),
         static_cast<double>(successfulReads) / std::chrono::duration<double>(elapsed).count()});

  start = Clock::now();
  std::size_t rejectedReads = 0;
  for (std::size_t index = 0; index < reads; ++index) {
    if (!files.read("../outside.bin", 0, 1).get().succeeded()) ++rejectedReads;
  }
  elapsed = Clock::now() - start;
  print({"async file traversal rejection", rejectedReads, milliseconds(elapsed),
         static_cast<double>(rejectedReads) / std::chrono::duration<double>(elapsed).count()});
  files.shutdown();

  constexpr std::size_t gpuCommands = 10000;
  aceps::gpu::GpuQueue queue(4);
  std::atomic<std::size_t> executed{0};
  std::vector<aceps::gpu::FenceValue> fences;
  fences.reserve(gpuCommands);
  start = Clock::now();
  for (std::size_t index = 0; index < gpuCommands; ++index) {
    fences.push_back(queue.submit([&executed] { executed.fetch_add(1, std::memory_order_relaxed); }));
  }
  bool allFencesWaited = true;
  for (const auto fence : fences) allFencesWaited = queue.wait(fence) && allFencesWaited;
  elapsed = Clock::now() - start;
  print({"GPU submit and fence wait", executed.load(), milliseconds(elapsed),
         static_cast<double>(executed.load()) / std::chrono::duration<double>(elapsed).count()});
  queue.shutdown();

  std::filesystem::remove_all(root);
  return (successfulReads == reads && rejectedReads == reads && allFencesWaited && executed == gpuCommands) ? 0 : 1;
}
