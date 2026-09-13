/*
 * SyscallRegistry.cpp implements deterministic HLE dispatch. Unknown calls
 * return the BSD-compatible ENOSYS value rather than silently succeeding.
 */
#include "aceps/os/SyscallRegistry.h"

#include <cerrno>
#include <utility>

namespace aceps::os {

bool SyscallRegistry::registerHandler(SyscallNumber number, SyscallHandler handler,
                                      std::string& error) {
  if (!handler) {
    error = "cannot register an empty syscall handler";
    return false;
  }
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
  const auto found = handlers_.find(number);
  if (found == handlers_.end()) return -static_cast<SyscallResult>(ENOSYS);
  return found->second(arguments);
}

bool SyscallRegistry::contains(SyscallNumber number) const noexcept {
  return handlers_.contains(number);
}

std::size_t SyscallRegistry::size() const noexcept { return handlers_.size(); }

} // namespace aceps::os
