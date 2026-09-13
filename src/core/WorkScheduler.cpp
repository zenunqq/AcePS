/*
 * WorkScheduler.cpp implements the bounded worker queue. Workers drain queued
 * tasks before exiting, while submitted exceptions remain in their futures.
 */
#include "aceps/core/WorkScheduler.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace aceps::core {

WorkScheduler::WorkScheduler(std::size_t workerCount, std::size_t queueCapacity)
    : queueCapacity_(std::max<std::size_t>(1, queueCapacity)) {
  if (workerCount == 0) workerCount = std::max<std::size_t>(1, std::thread::hardware_concurrency());
  workers_.reserve(workerCount);
  for (std::size_t index = 0; index < workerCount; ++index) workers_.emplace_back(&WorkScheduler::workerLoop, this);
}

WorkScheduler::~WorkScheduler() { shutdown(); }

void WorkScheduler::workerLoop() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock lock(mutex_);
      notEmpty_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
      if (stopping_ && tasks_.empty()) return;
      task = std::move(tasks_.front());
      tasks_.pop();
    }
    notFull_.notify_one();
    task();
  }
}

void WorkScheduler::shutdown() noexcept {
  {
    std::scoped_lock lock(mutex_);
    if (stopping_) return;
    stopping_ = true;
  }
  notEmpty_.notify_all();
  notFull_.notify_all();
  for (auto& worker : workers_) {
    if (worker.joinable()) worker.join();
  }
}

std::size_t WorkScheduler::workerCount() const noexcept { return workers_.size(); }

std::size_t WorkScheduler::pendingCount() const noexcept {
  std::scoped_lock lock(mutex_);
  return tasks_.size();
}

} // namespace aceps::core
