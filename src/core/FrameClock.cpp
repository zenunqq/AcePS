/*
 * FrameClock.cpp implements stable frame pacing. If emulation overruns a
 * deadline, the clock skips missed slots instead of accumulating latency.
 */
#include "aceps/core/FrameClock.h"

#include <algorithm>
#include <thread>
#include <stdexcept>

namespace aceps::core {

FrameClock::FrameClock(double targetFramesPerSecond) {
  if (!(targetFramesPerSecond > 0.0)) throw std::invalid_argument("target frame rate must be positive");
  framePeriod_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(1.0 / targetFramesPerSecond));
  if (framePeriod_.count() <= 0) framePeriod_ = std::chrono::nanoseconds(1);
  reset();
}

void FrameClock::reset() noexcept {
  nextFrame_ = Clock::now() + framePeriod_;
  lastDelta_ = std::chrono::nanoseconds(0);
  frameCount_ = 0;
}

void FrameClock::waitForNextFrame() {
  std::this_thread::sleep_until(nextFrame_);
  const auto now = Clock::now();
  lastDelta_ = std::chrono::duration_cast<std::chrono::nanoseconds>(now - (nextFrame_ - framePeriod_));
  ++frameCount_;
  nextFrame_ += framePeriod_;
  if (nextFrame_ < now) nextFrame_ = now + framePeriod_;
}

std::chrono::nanoseconds FrameClock::lastDelta() const noexcept { return lastDelta_; }
std::uint64_t FrameClock::frameCount() const noexcept { return frameCount_; }

} // namespace aceps::core
