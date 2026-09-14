/*
 * SyscallPatcher.cpp scans a caller-supplied executable guest mapping for the
 * two-byte x86-64 SYSCALL opcode. It deliberately performs no host trap work.
 */
#include "aceps/os/SyscallPatcher.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace aceps::os {

std::vector<void*> SyscallPatcher::scan(void* codeBase, const std::size_t codeSize) {
  std::vector<void*> sites;
  if (codeBase == nullptr || codeSize < 2) return sites;

  const auto base = reinterpret_cast<std::uintptr_t>(codeBase);
  if (codeSize > std::numeric_limits<std::uintptr_t>::max() - base) return sites;

  const auto* bytes = static_cast<const std::uint8_t*>(codeBase);
  for (std::size_t offset = 0; offset + 1 < codeSize; ++offset) {
    if (bytes[offset] != 0x0FU || bytes[offset + 1] != 0x05U) continue;
    sites.push_back(const_cast<std::uint8_t*>(bytes + offset));
    ++offset;
  }
  return sites;
}

} // namespace aceps::os
