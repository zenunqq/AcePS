// AcePS Jaguar CPU declaration: models independent guest-core register state and a small original x86-64 execution subset.
#pragma once

#include "engine/memory_space/address_space.h"

#include <array>
#include <cstdint>
#include <functional>

namespace AcePS::Processor {

enum class StepResult { executed, halted, memoryFault, unsupportedInstruction };

struct CoreState {
    std::array<std::uint64_t, 16> generalRegisters{};
    std::uint64_t instructionPointer{};
    bool halted{};
};

class JaguarCpu final {
public:
    static constexpr unsigned coreCount = 8;
    using SystemCallHandler = std::function<void(unsigned coreIndex, CoreState& state)>;

    explicit JaguarCpu(Memory::AddressSpace& memory);

    void reset(std::uint64_t entryPoint);
    void setSystemCallHandler(SystemCallHandler handler);
    StepResult step(unsigned coreIndex);
    [[nodiscard]] const CoreState& state(unsigned coreIndex) const;

private:
    bool fetch(unsigned coreIndex, std::span<std::byte> destination);

    Memory::AddressSpace& memory_;
    std::array<CoreState, coreCount> cores_{};
    SystemCallHandler systemCallHandler_;
};

} // namespace AcePS::Processor
