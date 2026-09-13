// AcePS processor tests: validate the original bootstrap decoder independently of the desktop interface.
#include "engine/memory_space/address_space.h"
#include "engine/processor/jaguar_cpu.h"

#include <array>
#include <iostream>

int main() {
    AcePS::Memory::AddressSpace memory;
    if (!memory.map(0x2000, 64, AcePS::Memory::Protection::read | AcePS::Memory::Protection::write | AcePS::Memory::Protection::execute)) {
        return 1;
    }
    const std::array<std::byte, 6> program{std::byte{0xB8}, std::byte{0x34}, std::byte{0x12}, std::byte{0x00}, std::byte{0x00}, std::byte{0xF4}};
    if (!memory.write(0x2000, program)) {
        return 1;
    }
    AcePS::Processor::JaguarCpu cpu(memory);
    cpu.reset(0x2000);
    if (cpu.step(0) != AcePS::Processor::StepResult::executed || cpu.state(0).generalRegisters[0] != 0x1234) {
        std::cerr << "MOV decoding failed.\n";
        return 1;
    }
    return cpu.step(0) == AcePS::Processor::StepResult::halted ? 0 : 1;
}
