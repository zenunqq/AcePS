// AcePS address-space implementation: provides synchronized sparse mappings with explicit range and permission validation.
#include "engine/memory_space/address_space.h"

#include <algorithm>
#include <mutex>

namespace AcePS::Memory {

bool AddressSpace::map(std::uint64_t address, std::size_t size, Protection protection) {
    if (!rangeFits(address, size)) {
        return false;
    }

    std::unique_lock lock(regionMutex_);
    const auto overlaps = [address, size](const Region& region) {
        const std::uint64_t requestedEnd = address + size;
        const std::uint64_t regionEnd = region.address + region.bytes.size();
        return address < regionEnd && region.address < requestedEnd;
    };
    if (std::any_of(regions_.begin(), regions_.end(), overlaps)) {
        return false;
    }

    regions_.push_back({address, protection, std::vector<std::byte>(size)});
    std::ranges::sort(regions_, {}, &Region::address);
    return true;
}

bool AddressSpace::unmap(std::uint64_t address, std::size_t size) {
    std::unique_lock lock(regionMutex_);
    const auto region = std::find_if(regions_.begin(), regions_.end(), [address, size](const Region& candidate) {
        return candidate.address == address && candidate.bytes.size() == size;
    });
    if (region == regions_.end()) {
        return false;
    }
    regions_.erase(region);
    return true;
}

bool AddressSpace::protect(std::uint64_t address, std::size_t size, Protection protection) {
    std::unique_lock lock(regionMutex_);
    const auto region = std::find_if(regions_.begin(), regions_.end(), [address, size](const Region& candidate) {
        return candidate.address == address && candidate.bytes.size() == size;
    });
    if (region == regions_.end()) {
        return false;
    }
    region->protection = protection;
    return true;
}

bool AddressSpace::write(std::uint64_t address, std::span<const std::byte> data) {
    std::shared_lock lock(regionMutex_);
    const auto region = std::find_if(regions_.begin(), regions_.end(), [address, &data](const Region& candidate) {
        return contains(candidate, address, data.size()) && hasPermission(candidate.protection, Protection::write);
    });
    if (region == regions_.end()) {
        return false;
    }
    std::copy(data.begin(), data.end(), region->bytes.begin() + static_cast<std::ptrdiff_t>(address - region->address));
    return true;
}

bool AddressSpace::read(std::uint64_t address, std::span<std::byte> destination) const {
    std::shared_lock lock(regionMutex_);
    const auto region = std::find_if(regions_.begin(), regions_.end(), [address, &destination](const Region& candidate) {
        return contains(candidate, address, destination.size()) && hasPermission(candidate.protection, Protection::read);
    });
    if (region == regions_.end()) {
        return false;
    }
    const auto offset = static_cast<std::ptrdiff_t>(address - region->address);
    std::copy(region->bytes.begin() + offset, region->bytes.begin() + offset + static_cast<std::ptrdiff_t>(destination.size()), destination.begin());
    return true;
}

bool AddressSpace::hasPermission(Protection protection, Protection required) {
    return (static_cast<unsigned>(protection) & static_cast<unsigned>(required)) != 0;
}

bool AddressSpace::rangeFits(std::uint64_t address, std::size_t size) {
    return size != 0 && size <= unifiedMemorySize && address <= unifiedMemorySize - size;
}

bool AddressSpace::contains(const Region& region, std::uint64_t address, std::size_t size) {
    return size <= region.bytes.size() && address >= region.address && address - region.address <= region.bytes.size() - size;
}

} // namespace AcePS::Memory
