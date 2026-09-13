// AcePS audio-output declaration: exposes a safe host-audio facade for future guest audio API calls.
#pragma once
#include <span>
#include <cstdint>
namespace AcePS::Sound { class AudioOutput { public: bool start(); void submit(std::span<const std::int16_t> interleavedSamples); void stop(); private: bool active_{}; }; }
