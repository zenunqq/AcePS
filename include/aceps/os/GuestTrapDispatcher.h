/*
 * GuestTrapDispatcher.h defines the host-independent boundary between a
 * patched x86-64 guest SYSCALL instruction and the Orbis syscall registry.
 * Platform adapters own signal/exception mechanics; this type owns dispatch
 * policy, trap-site validation, and lifecycle coordination.
 */
#pragma once

#include "aceps/os/SyscallRegistry.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace aceps::os {

// Register state captured at a patched guest trap site. The argument helpers
// implement the FreeBSD/Orbis x86-64 syscall ABI rather than the Linux ABI.
struct GuestRegisterFrame final {
  std::uint64_t rax{0};
  std::uint64_t rbx{0};
  std::uint64_t rcx{0};
  std::uint64_t rdx{0};
  std::uint64_t rsi{0};
  std::uint64_t rdi{0};
  std::uint64_t rbp{0};
  std::uint64_t rsp{0};
  std::uint64_t r8{0};
  std::uint64_t r9{0};
  std::uint64_t r10{0};
  std::uint64_t r11{0};
  std::uint64_t r12{0};
  std::uint64_t r13{0};
  std::uint64_t r14{0};
  std::uint64_t r15{0};
  std::uint64_t rip{0};
  std::uint64_t rflags{0};

  [[nodiscard]] std::uint64_t syscallNumber() const noexcept { return rax; }

  [[nodiscard]] std::uint64_t arg(int index) const noexcept {
    switch (index) {
    case 0:
      return rdi;
    case 1:
      return rsi;
    case 2:
      return rdx;
    case 3:
      return r10;
    case 4:
      return r8;
    case 5:
      return r9;
    default:
      return 0;
    }
  }

  void setReturnValue(SyscallResult value) noexcept { rax = static_cast<std::uint64_t>(value); }
};

class GuestTrapDispatcher final {
public:
  GuestTrapDispatcher() = default;
  ~GuestTrapDispatcher();

  GuestTrapDispatcher(const GuestTrapDispatcher&) = delete;
  GuestTrapDispatcher& operator=(const GuestTrapDispatcher&) = delete;

  // Installs the process-global platform trap adapter. Only one dispatcher may
  // be active in a process because signal and vectored exception handlers are
  // process-global resources.
  [[nodiscard]] bool install(SyscallRegistry& registry, std::string& error);

  // Restores patched instructions and the platform handler. Call only after
  // guest execution has been quiesced.
  void uninstall() noexcept;

  // Replaces a verified two-byte SYSCALL instruction with INT3; NOP. The NOP
  // consumes the second byte of the original instruction, so continuation at
  // RIP + 1 proceeds safely to the next guest instruction.
  [[nodiscard]] bool patchSite(void* instructionAddress, std::string& error);

  [[nodiscard]] bool installed() const noexcept;

  // Deterministic register-frame dispatch path. It intentionally contains no
  // platform signal or exception behavior, making it suitable for unit tests
  // and for platform adapters after a trap has been safely captured.
  static void dispatchFrame(GuestRegisterFrame& frame, SyscallRegistry& registry) noexcept;

private:
  [[nodiscard]] bool platformInstall(SyscallRegistry& registry, std::string& error);
  void platformUninstall() noexcept;
  [[nodiscard]] bool platformPatchSite(void* instructionAddress, std::string& error);

  SyscallRegistry* registry_{nullptr};
  mutable std::mutex mutex_;
  bool installed_{false};
};

} // namespace aceps::os
