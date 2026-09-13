// AcePS Jaguar CPU implementation: decodes safe bootstrap x86-64 instructions without borrowing emulator code.
#include "engine/processor/jaguar_cpu.h"

#include <array>
#include <stdexcept>

namespace AcePS::Processor {

JaguarCpu::JaguarCpu(Memory::AddressSpace& memory) : memory_(memory) {}

void JaguarCpu::reset(std::uint64_t entryPoint) {
    for (CoreState& core : cores_) {
        core = {};
        core.instructionPointer = entryPoint;
    }
}

void JaguarCpu::setSystemCallHandler(SystemCallHandler handler) {
    systemCallHandler_ = std::move(handler);
}

StepResult JaguarCpu::step(unsigned coreIndex) {
    if (coreIndex >= coreCount) {
        throw std::out_of_range("Guest core index is outside the Jaguar CPU range.");
    }

    CoreState& core = cores_[coreIndex];
    if (core.halted) {
        return StepResult::halted;
    }

    std::array<std::byte, 1> opcode{};
    if (!fetch(coreIndex, opcode)) {
        return StepResult::memoryFault;
    }

    const auto opcodeValue = std::to_integer<unsigned char>(opcode.front());
    if (opcodeValue == 0x90) { // NOP
        ++core.instructionPointer;
        return StepResult::executed;
    }
    if (opcodeValue == 0xF4) { // HLT
        core.halted = true;
        return StepResult::halted;
    }
    if (opcodeValue >= 0xB8 && opcodeValue <= 0xBF) { // MOV r64, imm32 in the current bootstrap subset.
        std::array<std::byte, 5> encodedInstruction{};
        if (!fetch(coreIndex, encodedInstruction)) {
            return StepResult::memoryFault;
        }
        std::uint32_t immediate = 0;
        for (unsigned byteIndex = 0; byteIndex < 4; ++byteIndex) {
            immediate |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(encodedInstruction[byteIndex + 1])) << (byteIndex * 8);
        }
        core.generalRegisters[opcodeValue - 0xB8] = immediate;
        core.instructionPointer += encodedInstruction.size();
        return StepResult::executed;
    }
    if (opcodeValue == 0x0F) { // SYSCALL is the only supported two-byte instruction at this stage.
        std::array<std::byte, 2> encodedInstruction{};
        if (!fetch(coreIndex, encodedInstruction)) {
            return StepResult::memoryFault;
        }
        if (std::to_integer<unsigned char>(encodedInstruction[1]) == 0x05 && systemCallHandler_) {
            core.instructionPointer += encodedInstruction.size();
            systemCallHandler_(coreIndex, core);
            return StepResult::executed;
        }
    }

    return StepResult::unsupportedInstruction;
}

const CoreState& JaguarCpu::state(unsigned coreIndex) const {
    return cores_.at(coreIndex);
}

bool JaguarCpu::fetch(unsigned coreIndex, std::span<std::byte> destination) {
    return memory_.read(cores_[coreIndex].instructionPointer, destination);
}

} // namespace AcePS::Processor
