/*
 * GpuQueue.cpp implements ordered fence tracking over the generic scheduler.
 * A promise is registered before scheduling so waiters can safely observe a
 * submitted fence without racing the worker thread.
 */
#include "aceps/gpu/GpuQueue.h"

#include <mutex>
#include <exception>
#include <stdexcept>

namespace aceps::gpu {

GpuQueue::GpuQueue(std::size_t workers) : scheduler_(workers, 1024) {}
GpuQueue::~GpuQueue() { shutdown(); }

FenceValue GpuQueue::submit(std::function<void()> command) {
  if (!command) throw std::invalid_argument("GPU command must not be empty");
  const auto promise = std::make_shared<std::promise<void>>();
  const auto future = promise->get_future().share();
  FenceValue fence = 0;
  {
    std::scoped_lock lock(mutex_);
    fence = nextFence_++;
    inFlight_.emplace(fence, future);
  }
  try {
    (void)scheduler_.submit([this, fence, command = std::move(command), promise]() mutable {
      try {
        command();
        promise->set_value();
      } catch (...) {
        promise->set_exception(std::current_exception());
      }
      std::scoped_lock completionLock(mutex_);
      completedOutOfOrder_.insert(fence);
      while (completedOutOfOrder_.contains(completedFence_ + 1U)) {
        completedOutOfOrder_.erase(completedFence_ + 1U);
        ++completedFence_;
      }
    });
  } catch (...) {
    std::scoped_lock lock(mutex_);
    inFlight_.erase(fence);
    throw;
  }
  return fence;
}

bool GpuQueue::wait(FenceValue fence) {
  std::shared_future<void> future;
  {
    std::scoped_lock lock(mutex_);
    const auto found = inFlight_.find(fence);
    if (found != inFlight_.end()) {
      future = found->second;
    } else if (fence <= completedFence_) {
      return true;
    } else {
      return false;
    }
  }
  future.wait();
  try {
    future.get();
  } catch (...) {
    std::scoped_lock lock(mutex_);
    inFlight_.erase(fence);
    throw;
  }
  {
    std::scoped_lock lock(mutex_);
    inFlight_.erase(fence);
  }
  return true;
}

FenceValue GpuQueue::completedFence() const noexcept {
  std::scoped_lock lock(mutex_);
  return completedFence_;
}

void GpuQueue::shutdown() noexcept { scheduler_.shutdown(); }

} // namespace aceps::gpu
