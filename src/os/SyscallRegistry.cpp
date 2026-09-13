/*
 * SyscallRegistry.cpp implements deterministic HLE dispatch. Registration is
 * exclusive; dispatch takes a shared snapshot of the handler and is safe for
 * concurrent guest threads. Unknown calls return BSD-compatible ENOSYS.
 */
#include "aceps/os/SyscallRegistry.h"

#include <cerrno>
#include <mutex>
#include <utility>

namespace aceps::os {

bool SyscallRegistry::registerHandler(SyscallNumber number, SyscallHandler handler,
                                      std::string& error) {
  if (!handler) {
    error = "cannot register an empty syscall handler";
    return false;
  }
  std::unique_lock lock(mutex_);
  if (handlers_.contains(number)) {
    error = "syscall handler already registered: " + std::to_string(number);
    return false;
  }
  handlers_.emplace(number, std::move(handler));
  error.clear();
  return true;
}

SyscallResult SyscallRegistry::dispatch(SyscallNumber number,
                                        const std::vector<std::uint64_t>& arguments) const {
  dispatchCount_.fetch_add(1, std::memory_order_relaxed);
  SyscallHandler handler;
  {
    std::shared_lock lock(mutex_);
    const auto found = handlers_.find(number);
    if (found == handlers_.end()) return -static_cast<SyscallResult>(ENOSYS);
    handler = found->second;
  }
  return handler(arguments);
}

bool SyscallRegistry::contains(SyscallNumber number) const noexcept {
  std::shared_lock lock(mutex_);
  return handlers_.contains(number);
}

std::size_t SyscallRegistry::size() const noexcept {
  std::shared_lock lock(mutex_);
  return handlers_.size();
}

std::size_t SyscallRegistry::dispatchCount() const noexcept {
  return dispatchCount_.load(std::memory_order_relaxed);
}

} // namespace aceps::os
