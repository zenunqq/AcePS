// AcePS address-space tests: verify mapping boundaries, access permissions, and lifecycle operations.
#include "engine/memory_space/address_space.h"

#include <array>
#include <iostream>

int main() {
    using namespace AcePS::Memory;
    AddressSpace memory;
    if (!memory.map(0x1000, 16, Protection::read | Protection::write) || memory.map(0x1008, 16, Protection::read)) {
        std::cerr << "Mapping overlap validation failed.\n";
        return 1;
    }

    const std::array<std::byte, 2> source{std::byte{0xCA}, std::byte{0xFE}};
    std::array<std::byte, 2> destination{};
    if (!memory.write(0x1004, source) || !memory.read(0x1004, destination) || destination != source) {
        std::cerr << "Mapped read/write round trip failed.\n";
        return 1;
    }
    if (!memory.protect(0x1000, 16, Protection::read) || memory.write(0x1004, source)) {
        std::cerr << "Protection transition failed.\n";
        return 1;
    }
    if (!memory.unmap(0x1000, 16) || memory.read(0x1004, destination)) {
        std::cerr << "Unmap operation failed.\n";
        return 1;
    }
    return memory.map(AddressSpace::unifiedMemorySize, 1, Protection::read) ? 1 : 0;
}
