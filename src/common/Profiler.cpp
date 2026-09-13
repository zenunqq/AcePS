/*
 * Profiler.cpp implements relaxed atomic profiling updates. The profiler is
 * designed for diagnostics and avoids participating in subsystem locking.
 */
#include "aceps/common/Profiler.h"

namespace aceps::common {

void ProfileCounter::record(std::chrono::nanoseconds elapsed) noexcept {
  events_.fetch_add(1, std::memory_order_relaxed);
  nanoseconds_.fetch_add(static_cast<std::uint64_t>(elapsed.count()), std::memory_order_relaxed);
}

ProfileSnapshot ProfileCounter::snapshot() const noexcept {
  return {events_.load(std::memory_order_relaxed), nanoseconds_.load(std::memory_order_relaxed)};
}

void ProfileCounter::reset() noexcept {
  events_.store(0, std::memory_order_relaxed);
  nanoseconds_.store(0, std::memory_order_relaxed);
}

ProfileScope::ProfileScope(ProfileCounter& counter) noexcept
    : counter_(counter), start_(std::chrono::steady_clock::now()) {}

ProfileScope::~ProfileScope() {
  counter_.record(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - start_));
}

} // namespace aceps::common
