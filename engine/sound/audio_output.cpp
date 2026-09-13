// AcePS audio-output implementation: accepts early audio calls without crashing until a backend is integrated.
#include "engine/sound/audio_output.h"
namespace AcePS::Sound { bool AudioOutput::start(){active_=true;return true;} void AudioOutput::submit(std::span<const std::int16_t>){/* TODO: send samples to a cross-platform host audio backend. */} void AudioOutput::stop(){active_=false;} }
