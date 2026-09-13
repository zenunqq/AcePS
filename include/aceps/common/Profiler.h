/*
 * Profiler.h provides cheap atomic counters and scoped timing. Counters are
 * always available for diagnostics; callers can expose snapshots through the
 * GUI without adding locks to emulation hot paths.
 */
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace aceps::common {

struct ProfileSnapshot final {
  std::uint64_t events;
  std::uint64_t nanoseconds;
};

class ProfileCounter final {
public:
  void record(std::chrono::nanoseconds elapsed) noexcept;
  [[nodiscard]] ProfileSnapshot snapshot() const noexcept;
  void reset() noexcept;

private:
  std::atomic<std::uint64_t> events_{0};
  std::atomic<std::uint64_t> nanoseconds_{0};
};

class ProfileScope final {
public:
  explicit ProfileScope(ProfileCounter& counter) noexcept;
  ~ProfileScope();
  ProfileScope(const ProfileScope&) = delete;
  ProfileScope& operator=(const ProfileScope&) = delete;

private:
  ProfileCounter& counter_;
  std::chrono::steady_clock::time_point start_;
};

} // namespace aceps::common
