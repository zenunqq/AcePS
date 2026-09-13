// AcePS emulation-session declaration: coordinates the initial CPU and memory lifecycle for one loaded title.
#pragma once
#include "engine/memory_space/address_space.h"
#include "engine/processor/jaguar_cpu.h"
#include <string>
namespace AcePS::Runtime { class EmulationSession { public: EmulationSession(); bool loadBootstrap(std::string& reason); bool runOneFrame(); private: Memory::AddressSpace memory_; Processor::JaguarCpu cpu_; }; }
