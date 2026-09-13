// AcePS emulation-session implementation: creates a deterministic bootstrap guest and maps execution outcomes to a frame loop.
#include "runtime/emulation_session.h"

#include <array>

namespace AcePS::Runtime {

EmulationSession::EmulationSession() : cpu_(memory_) {}

bool EmulationSession::loadBootstrap(std::string& reason) {
    constexpr std::uint64_t bootstrapAddress = 0x1000;
    if (!memory_.map(bootstrapAddress, 4096, Memory::Protection::read | Memory::Protection::write | Memory::Protection::execute)) {
        reason = "Unable to reserve guest bootstrap memory.";
        return false;
    }

    const std::array<std::byte, 1> haltInstruction{std::byte{0xF4}};
    if (!memory_.write(bootstrapAddress, haltInstruction)) {
        reason = "Unable to write guest bootstrap code.";
        return false;
    }

    cpu_.reset(bootstrapAddress);
    reason = "Bootstrap loaded.";
    return true;
}

bool EmulationSession::runOneFrame() {
    return cpu_.step(0) == Processor::StepResult::executed;
}

} // namespace AcePS::Runtime
