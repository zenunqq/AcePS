/* AudioSystem.cpp implements fixed-capacity PCM ports and a software-safe backend. */
#include "aceps/audio/AudioSystem.h"
#include "aceps/audio/SceAudioOut.h"
#include "aceps/os/SyscallRegistry.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace aceps::audio {
namespace {
std::size_t sampleSize(AudioFormat format) noexcept { return format == AudioFormat::S16Mono || format == AudioFormat::S16Stereo ? sizeof(std::int16_t) : sizeof(float); }

struct SoftwareBackend final {
  bool open(AudioFormat, std::uint32_t rate, std::uint32_t channels, std::string& error) noexcept {
    if (rate == 0 || channels == 0) { error = "invalid audio device format"; return false; }
    error.clear(); return true;
  }
  void close() noexcept {}
  void pause(bool) noexcept {}
};
} // namespace

struct AudioSystem::Port final {
  std::int32_t handle{0};
  AudioFormat format{AudioFormat::S16Stereo};
  std::uint32_t sampleRate{48000};
  std::uint32_t framesPerBlock{256};
  std::size_t channels{2};
  std::int32_t volume[2]{32768, 32768};
  std::unique_ptr<AudioRingBuffer<std::int16_t>> s16;
  std::unique_ptr<AudioRingBuffer<float>> floats;
  SoftwareBackend backend;

  [[nodiscard]] std::size_t available() const noexcept { return s16 ? s16->available() : floats->available(); }
  [[nodiscard]] std::uint64_t underruns() const noexcept { return s16 ? s16->underrun_count() : floats->underrun_count(); }
};

AudioSystem::AudioSystem() = default;
AudioSystem::~AudioSystem() { shutdown(); }

bool AudioSystem::initialize(std::string& error) { initialized_ = true; error.clear(); return true; }

void AudioSystem::shutdown() noexcept {
  std::scoped_lock lock(mutex_);
  for (auto& port : ports_) if (port) { port->backend.close(); port.reset(); }
  initialized_ = false;
}

bool AudioSystem::open(AudioFormat format, std::uint32_t sampleRate, std::uint32_t channels,
                       std::uint32_t framesPerBlock, std::int32_t& handle, std::string& error) {
  if (!initialized_) { error = "audio system is not initialized"; return false; }
  if ((sampleRate != 48000 && sampleRate != 44100) || channels == 0 || channels > 2 || framesPerBlock == 0) {
    error = "invalid audio port format"; return false;
  }
  std::scoped_lock lock(mutex_);
  const auto found = std::find_if(ports_.begin(), ports_.end(), [](const auto& port) { return !port; });
  if (found == ports_.end()) { error = "audio port table is full"; return false; }
  const auto index = static_cast<std::int32_t>(std::distance(ports_.begin(), found));
  auto port = std::make_unique<Port>();
  port->handle = index + 1;
  port->format = format;
  port->sampleRate = sampleRate;
  port->framesPerBlock = framesPerBlock;
  port->channels = channels;
  if (format == AudioFormat::S16Mono || format == AudioFormat::S16Stereo) port->s16 = std::make_unique<AudioRingBuffer<std::int16_t>>(channels);
  else port->floats = std::make_unique<AudioRingBuffer<float>>(channels);
  if (!port->backend.open(format, sampleRate, channels, error)) return false;
  *found = std::move(port);
  handle = index + 1;
  error.clear(); return true;
}

AudioSystem::Port* AudioSystem::findPort(std::int32_t handle) const noexcept {
  if (handle < 1 || handle > static_cast<std::int32_t>(ports_.size())) return nullptr;
  return ports_[static_cast<std::size_t>(handle - 1)].get();
}

std::int32_t AudioSystem::output(std::int32_t handle, const void* pcm) {
  std::unique_lock lock(mutex_);
  auto* port = findPort(handle);
  if (port == nullptr) return SCE_AUDIO_OUT_ERROR_NOT_OPENED;
  const auto frames = port->framesPerBlock;
  const auto bytes = frames * port->channels * sampleSize(port->format);
  std::vector<std::uint8_t> silence(bytes, 0);
  const auto* source = pcm == nullptr ? silence.data() : static_cast<const std::uint8_t*>(pcm);
  while (port->available() > (kAudioRingCapacityFrames * 3U) / 4U) {
    if (spaceAvailable_.wait_for(lock, std::chrono::milliseconds(20)) == std::cv_status::timeout) break;
  }
  if (port->format == AudioFormat::S16Mono || port->format == AudioFormat::S16Stereo) {
    std::vector<std::int16_t> scaled(frames * port->channels);
    const auto* input = reinterpret_cast<const std::int16_t*>(source);
    for (std::size_t i = 0; i < scaled.size(); ++i) {
      const auto channel = i % port->channels;
      const auto value = static_cast<std::int64_t>(input[i]) * port->volume[channel];
      scaled[i] = static_cast<std::int16_t>(std::clamp(value / 32768, static_cast<std::int64_t>(-32768), static_cast<std::int64_t>(32767)));
    }
    port->s16->push(scaled.data(), frames);
  } else {
    std::vector<float> scaled(frames * port->channels);
    const auto* input = reinterpret_cast<const float*>(source);
    for (std::size_t i = 0; i < scaled.size(); ++i) scaled[i] = input[i] * static_cast<float>(port->volume[i % port->channels]) / 32768.0F;
    port->floats->push(scaled.data(), frames);
  }
  lock.unlock(); spaceAvailable_.notify_all(); return 0;
}

std::int32_t AudioSystem::outputs(const void* portOutputs, std::size_t count) {
  if (portOutputs == nullptr || count > ports_.size()) return SCE_AUDIO_OUT_ERROR_INVALID_PORT;
  const auto* outputs = static_cast<const SceAudioOutPortOutput*>(portOutputs);
  for (std::size_t i = 0; i < count; ++i) { const auto result = output(outputs[i].handle, outputs[i].ptr); if (result != 0) return result; }
  return 0;
}

std::int32_t AudioSystem::close(std::int32_t handle) {
  std::scoped_lock lock(mutex_);
  auto* port = findPort(handle);
  if (port == nullptr) return SCE_AUDIO_OUT_ERROR_NOT_OPENED;
  port->backend.close(); ports_[static_cast<std::size_t>(handle - 1)].reset(); return 0;
}

std::int32_t AudioSystem::setVolume(std::int32_t handle, std::uint32_t flags, const std::int32_t* volumes) {
  std::scoped_lock lock(mutex_);
  auto* port = findPort(handle);
  if (port == nullptr) return SCE_AUDIO_OUT_ERROR_NOT_OPENED;
  if (volumes == nullptr || (flags & ~3U) != 0) return SCE_AUDIO_OUT_ERROR_INVALID_VOLUME;
  for (std::size_t channel = 0; channel < 2; ++channel) if ((flags & (1U << channel)) != 0) {
    if (volumes[channel] < 0 || volumes[channel] > 32768) return SCE_AUDIO_OUT_ERROR_INVALID_VOLUME;
    port->volume[channel] = volumes[channel];
  }
  return 0;
}

std::int32_t AudioSystem::getPortState(std::int32_t handle, void* state) const {
  std::scoped_lock lock(mutex_);
  auto* port = findPort(handle);
  if (port == nullptr) return SCE_AUDIO_OUT_ERROR_NOT_OPENED;
  if (state == nullptr) return SCE_AUDIO_OUT_ERROR_INVALID_PORT;
  auto* result = static_cast<SceAudioOutPortState*>(state);
  result->output = 1; result->channel = static_cast<std::int32_t>(port->channels);
  result->volume[0] = port->volume[0]; result->volume[1] = port->volume[1]; result->rerouteCounter = 0; return 0;
}

std::int32_t AudioSystem::getSystemState(void* state) const {
  if (state == nullptr) return SCE_AUDIO_OUT_ERROR_INVALID_PORT;
  auto* result = static_cast<SceAudioOutSystemState*>(state); result->loudness = -18.0F; result->speakerVolume = 85; return 0;
}

std::uint64_t AudioSystem::audioUnderruns() const noexcept {
  std::scoped_lock lock(mutex_); std::uint64_t total = 0; for (const auto& port : ports_) if (port) total += port->underruns(); return total;
}

} // namespace aceps::audio
