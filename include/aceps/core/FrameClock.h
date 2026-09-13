/*
 * FrameClock.h provides monotonic frame pacing. It avoids wall-clock jumps and
 * reports frame deltas for future vsync, emulation, and audio synchronization.
 */
#pragma once

#include <chrono>
#include <cstdint>

namespace aceps::core {

class FrameClock final {
public:
  using Clock = std::chrono::steady_clock;
  explicit FrameClock(double targetFramesPerSecond = 60.0);

  void reset() noexcept;
  void waitForNextFrame();
  [[nodiscard]] std::chrono::nanoseconds lastDelta() const noexcept;
  [[nodiscard]] std::uint64_t frameCount() const noexcept;

private:
  Clock::time_point nextFrame_;
  std::chrono::nanoseconds framePeriod_;
  std::chrono::nanoseconds lastDelta_{0};
  std::uint64_t frameCount_{0};
};

} // namespace aceps::core
