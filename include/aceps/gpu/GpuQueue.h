/*
 * GpuQueue.h models asynchronous graphics/compute submission. It provides
 * monotonically increasing fence values so the CPU can wait without blocking
 * command construction or confusing queue completion with submission.
 */
#pragma once

#include "aceps/core/WorkScheduler.h"

#include <cstdint>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>

namespace aceps::gpu {

using FenceValue = std::uint64_t;

class GpuQueue final {
public:
  explicit GpuQueue(std::size_t workers = 1);
  ~GpuQueue();

  [[nodiscard]] FenceValue submit(std::function<void()> command);
  [[nodiscard]] bool wait(FenceValue fence);
  [[nodiscard]] FenceValue completedFence() const noexcept;
  void shutdown() noexcept;

private:
  struct Submission;
  core::WorkScheduler scheduler_;
  mutable std::mutex mutex_;
  FenceValue nextFence_{1};
  FenceValue completedFence_{0};
  std::unordered_map<FenceValue, std::shared_future<void>> inFlight_;
  std::set<FenceValue> completedOutOfOrder_;
};

} // namespace aceps::gpu
