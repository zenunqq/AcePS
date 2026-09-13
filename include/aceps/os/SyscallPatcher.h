/*
 * SyscallPatcher.h defines the platform boundary that redirects x86-64 guest
 * syscall instructions into the Orbis syscall registry.
 *
 * Only one patcher may be installed per process. Signal and exception handlers
 * are process-global, so callers must keep the registry alive until uninstall.
 */
#pragma once

#include "aceps/os/SyscallRegistry.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace aceps::os {

struct SyscallPatcherState;

class SyscallPatcher final {
public:
  explicit SyscallPatcher(SyscallRegistry& registry) noexcept;
  ~SyscallPatcher();

  SyscallPatcher(const SyscallPatcher&) = delete;
  SyscallPatcher& operator=(const SyscallPatcher&) = delete;

  [[nodiscard]] bool install(std::string& error);
  [[nodiscard]] bool uninstall(std::string& error);
  [[nodiscard]] bool installed() const noexcept;

private:
  SyscallRegistry& registry_;
  std::unique_ptr<SyscallPatcherState> state_;
  mutable std::mutex mutex_;
  std::atomic<bool> installed_{false};
};

} // namespace aceps::os
