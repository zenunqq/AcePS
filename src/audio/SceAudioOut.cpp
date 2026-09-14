/* SceAudioOut.cpp registers the initial libSceAudioOut HLE exports. */
#include "aceps/audio/SceAudioOut.h"
#include "aceps/audio/AudioSystem.h"
#include "aceps/os/SyscallRegistry.h"

#include <cerrno>

namespace aceps::audio {
namespace {
constexpr std::uint32_t kOpen = 608;
constexpr std::uint32_t kClose = 609;
constexpr std::uint32_t kOutput = 610;
constexpr std::uint32_t kOutputs = 611;
constexpr std::uint32_t kSetVolume = 612;
constexpr std::uint32_t kGetPortState = 613;
constexpr std::uint32_t kGetSystemState = 614;
}

bool SceAudioOut::registerHandlers(AudioSystem& audio, os::SyscallRegistry& registry, std::string& error) {
  const auto add = [&registry, &error](std::uint32_t number, os::SyscallHandler handler) {
    return registry.registerHandler(number, std::move(handler), error);
  };
  if (!add(kOpen, [&audio](const auto& args) {
    if (args.size() < 6) return static_cast<os::SyscallResult>(-EINVAL);
    const auto format = args[5] & 3U;
    const auto channels = (format == 0 || format == 2) ? 1U : 2U;
    const auto audioFormat = format < 2 ? (channels == 1 ? AudioFormat::S16Mono : AudioFormat::S16Stereo)
                                       : (channels == 1 ? AudioFormat::FloatMono : AudioFormat::FloatStereo);
    std::int32_t handle = 0; std::string localError;
    if (!audio.open(audioFormat, static_cast<std::uint32_t>(args[4]), channels,
                    static_cast<std::uint32_t>(args[3]), handle, localError)) return static_cast<os::SyscallResult>(SCE_AUDIO_OUT_ERROR_PORT_FULL);
    return static_cast<os::SyscallResult>(handle);
  }) ||
      !add(kClose, [&audio](const auto& args) { return args.empty() ? static_cast<os::SyscallResult>(-EINVAL) : audio.close(static_cast<std::int32_t>(args[0])); }) ||
      !add(kOutput, [&audio](const auto& args) { return args.size() < 2 ? static_cast<os::SyscallResult>(-EINVAL) : audio.output(static_cast<std::int32_t>(args[0]), reinterpret_cast<const void*>(static_cast<std::uintptr_t>(args[1]))); }) ||
      !add(kOutputs, [&audio](const auto& args) { return args.size() < 2 ? static_cast<os::SyscallResult>(-EINVAL) : audio.outputs(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(args[0])), static_cast<std::size_t>(args[1])); }) ||
      !add(kSetVolume, [&audio](const auto& args) { return args.size() < 3 ? static_cast<os::SyscallResult>(-EINVAL) : audio.setVolume(static_cast<std::int32_t>(args[0]), static_cast<std::uint32_t>(args[1]), reinterpret_cast<const std::int32_t*>(static_cast<std::uintptr_t>(args[2]))); }) ||
      !add(kGetPortState, [&audio](const auto& args) { return args.size() < 2 ? static_cast<os::SyscallResult>(-EINVAL) : audio.getPortState(static_cast<std::int32_t>(args[0]), reinterpret_cast<void*>(static_cast<std::uintptr_t>(args[1]))); }) ||
      !add(kGetSystemState, [&audio](const auto& args) { return args.empty() ? static_cast<os::SyscallResult>(-EINVAL) : audio.getSystemState(reinterpret_cast<void*>(static_cast<std::uintptr_t>(args[0]))); })) return false;
  error.clear(); return true;
}

} // namespace aceps::audio
