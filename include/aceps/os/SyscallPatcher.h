/*
 * SyscallPatcher.h identifies x86-64 SYSCALL instructions in a known guest
 * executable mapping. GuestTrapDispatcher owns actual instruction patching
 * and process-global trap-handler lifecycle.
 */
#pragma once

#include <cstddef>
#include <vector>

namespace aceps::os {

class SyscallPatcher final {
public:
  // Returns addresses of complete 0F 05 instructions within the caller's
  // trusted executable mapping. The scanner performs no writes and never
  // dereferences memory outside [codeBase, codeBase + codeSize).
  [[nodiscard]] static std::vector<void*> scan(void* codeBase, std::size_t codeSize);
};

} // namespace aceps::os
