/*
 * WorkScheduler.h defines a bounded multi-worker queue for guest-side jobs.
 * It provides backpressure, deterministic shutdown, and exception isolation so
 * one faulty task cannot terminate an emulator worker thread.
 */
#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace aceps::core {

class WorkScheduler final {
public:
  explicit WorkScheduler(std::size_t workerCount = 0, std::size_t queueCapacity = 4096);
  ~WorkScheduler();

  WorkScheduler(const WorkScheduler&) = delete;
  WorkScheduler& operator=(const WorkScheduler&) = delete;

  template <typename Function>
  [[nodiscard]] auto submit(Function&& function) -> std::future<std::invoke_result_t<Function>> {
    using ReturnType = std::invoke_result_t<Function>;
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(std::forward<Function>(function));
    auto result = task->get_future();
    {
      std::unique_lock lock(mutex_);
      notFull_.wait(lock, [this] { return stopping_ || tasks_.size() < queueCapacity_; });
      if (stopping_) {
        throw std::runtime_error("cannot submit work after scheduler shutdown");
      }
      tasks_.emplace([task = std::move(task)]() mutable { (*task)(); });
    }
    notEmpty_.notify_one();
    return result;
  }

  void shutdown() noexcept;
  [[nodiscard]] std::size_t workerCount() const noexcept;
  [[nodiscard]] std::size_t pendingCount() const noexcept;

private:
  void workerLoop();
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  const std::size_t queueCapacity_;
  mutable std::mutex mutex_;
  std::condition_variable notEmpty_;
  std::condition_variable notFull_;
  bool stopping_{false};
};

} // namespace aceps::core
