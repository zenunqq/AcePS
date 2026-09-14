/* libSceAudioOut HLE constants, state records, and syscall registration. */
#pragma once

#include <cstdint>
#include <string>

namespace aceps::os { class SyscallRegistry; }
namespace aceps::audio {

constexpr std::int32_t SCE_AUDIO_OUT_ERROR_NOT_OPENED = static_cast<std::int32_t>(0x8026000B);
constexpr std::int32_t SCE_AUDIO_OUT_ERROR_BUSY = static_cast<std::int32_t>(0x8026000C);
constexpr std::int32_t SCE_AUDIO_OUT_ERROR_INVALID_PORT = static_cast<std::int32_t>(0x8026000D);
constexpr std::int32_t SCE_AUDIO_OUT_ERROR_INVALID_FREQ = static_cast<std::int32_t>(0x8026000E);
constexpr std::int32_t SCE_AUDIO_OUT_ERROR_INVALID_VOLUME = static_cast<std::int32_t>(0x8026000F);
constexpr std::int32_t SCE_AUDIO_OUT_ERROR_PORT_FULL = static_cast<std::int32_t>(0x80260010);

struct SceAudioOutPortState final {
  std::int32_t output{0};
  std::int32_t channel{0};
  std::int32_t volume[4]{0, 0, 0, 0};
  std::uint32_t rerouteCounter{0};
};

struct SceAudioOutSystemState final {
  float loudness{-18.0F};
  std::int32_t speakerVolume{85};
};

struct SceAudioOutPortOutput final {
  std::int32_t handle{0};
  const void* ptr{nullptr};
};

class AudioSystem;
class SceAudioOut final {
public:
  static bool registerHandlers(AudioSystem& audio, os::SyscallRegistry& registry,
                               std::string& error);
};

} // namespace aceps::audio
