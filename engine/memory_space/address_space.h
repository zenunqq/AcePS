// AcePS address-space declaration: manages sparse protected mappings inside the PS4's unified guest-memory range.
#pragma once

#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <span>
#include <vector>

namespace AcePS::Memory {

enum class Protection : std::uint8_t {
    none = 0,
    read = 1,
    write = 2,
    execute = 4,
};

constexpr Protection operator|(Protection left, Protection right) {
    return static_cast<Protection>(static_cast<unsigned>(left) | static_cast<unsigned>(right));
}

class AddressSpace final {
public:
    static constexpr std::uint64_t unifiedMemorySize = 8ULL * 1024 * 1024 * 1024;

    bool map(std::uint64_t address, std::size_t size, Protection protection);
    bool unmap(std::uint64_t address, std::size_t size);
    bool protect(std::uint64_t address, std::size_t size, Protection protection);
    bool write(std::uint64_t address, std::span<const std::byte> data);
    bool read(std::uint64_t address, std::span<std::byte> destination) const;

private:
    struct Region {
        std::uint64_t address;
        Protection protection;
        std::vector<std::byte> bytes;
    };

    [[nodiscard]] static bool hasPermission(Protection protection, Protection required);
    [[nodiscard]] static bool rangeFits(std::uint64_t address, std::size_t size);
    [[nodiscard]] static bool contains(const Region& region, std::uint64_t address, std::size_t size);

    mutable std::shared_mutex regionMutex_;
    std::vector<Region> regions_;
};

} // namespace AcePS::Memory
