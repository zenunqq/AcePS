/*
 * AudioSystem.h provides fixed-capacity SPSC audio rings and the AudioOut
 * port manager. The software backend remains usable when SDL3 is unavailable.
 */
#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace aceps::os { class SyscallRegistry; }

namespace aceps::audio {

enum class AudioFormat : std::uint8_t { S16Mono, S16Stereo, FloatMono, FloatStereo };

constexpr std::size_t kAudioRingCapacityFrames = 8192;

// Single-producer/single-consumer ring. Storage is allocated once at construction.
template <typename Sample, std::size_t Capacity = kAudioRingCapacityFrames>
class AudioRingBuffer final {
public:
  explicit AudioRingBuffer(std::size_t channels = 1) : channels_(channels == 0 ? 1 : channels) {}

  std::size_t push(const void* frames, std::size_t count) noexcept {
    if (frames == nullptr || count == 0) return 0;
    const auto* source = static_cast<const Sample*>(frames);
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    const auto available = Capacity - (head - tail);
    const auto written = count < available ? count : available;
    for (std::size_t frame = 0; frame < written; ++frame) {
      for (std::size_t channel = 0; channel < channels_; ++channel) {
        samples_[((head + frame) % Capacity) * channels_ + channel] = source[frame * channels_ + channel];
      }
    }
    head_.store(head + written, std::memory_order_release);
    return written;
  }

  std::size_t pop(void* destination, std::size_t count) noexcept {
    if (destination == nullptr || count == 0) return 0;
    auto* output = static_cast<Sample*>(destination);
    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    const auto readable = head - tail;
    const auto read = count < readable ? count : readable;
    for (std::size_t frame = 0; frame < read; ++frame) {
      for (std::size_t channel = 0; channel < channels_; ++channel) {
        output[frame * channels_ + channel] = samples_[((tail + frame) % Capacity) * channels_ + channel];
      }
    }
    if (read < count) underruns_.fetch_add(1, std::memory_order_relaxed);
    tail_.store(tail + read, std::memory_order_release);
    return read;
  }

  [[nodiscard]] std::size_t available() const noexcept {
    return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::uint64_t underrun_count() const noexcept { return underruns_.load(std::memory_order_relaxed); }

private:
  std::array<Sample, Capacity * 2> samples_{};
  std::size_t channels_{1};
  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};
  std::atomic<std::uint64_t> underruns_{0};
};

class AudioSystem final {
public:
  AudioSystem();
  ~AudioSystem();
  AudioSystem(const AudioSystem&) = delete;
  AudioSystem& operator=(const AudioSystem&) = delete;

  [[nodiscard]] bool initialize(std::string& error);
  void shutdown() noexcept;
  [[nodiscard]] bool open(AudioFormat format, std::uint32_t sampleRate,
                          std::uint32_t channels, std::uint32_t framesPerBlock,
                          std::int32_t& handle, std::string& error);
  [[nodiscard]] std::int32_t output(std::int32_t handle, const void* pcm);
  [[nodiscard]] std::int32_t outputs(const void* portOutputs, std::size_t count);
  [[nodiscard]] std::int32_t close(std::int32_t handle);
  [[nodiscard]] std::int32_t setVolume(std::int32_t handle, std::uint32_t flags,
                                        const std::int32_t* volumes);
  [[nodiscard]] std::int32_t getPortState(std::int32_t handle, void* state) const;
  [[nodiscard]] std::int32_t getSystemState(void* state) const;
  [[nodiscard]] std::uint64_t audioUnderruns() const noexcept;
  [[nodiscard]] bool registerHandlers(os::SyscallRegistry& registry, std::string& error);

private:
  struct Port;
  [[nodiscard]] Port* findPort(std::int32_t handle) const noexcept;
  std::array<std::unique_ptr<Port>, 8> ports_{};
  mutable std::mutex mutex_;
  std::condition_variable spaceAvailable_;
  bool initialized_{false};
};

} // namespace aceps::audio
