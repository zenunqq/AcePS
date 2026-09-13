/*
 * SyscallRegistry.h defines the Orbis HLE syscall dispatch boundary. Handlers
 * receive raw guest arguments and return the guest-visible signed result.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace aceps::os {

using SyscallNumber = std::uint32_t;
using SyscallResult = std::int64_t;
using SyscallHandler = std::function<SyscallResult(const std::vector<std::uint64_t>&)>;

class SyscallRegistry final {
public:
  [[nodiscard]] bool registerHandler(SyscallNumber number, SyscallHandler handler,
                                      std::string& error);
  [[nodiscard]] SyscallResult dispatch(SyscallNumber number,
                                       const std::vector<std::uint64_t>& arguments) const;
  [[nodiscard]] bool contains(SyscallNumber number) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;

private:
  std::unordered_map<SyscallNumber, SyscallHandler> handlers_;
};

} // namespace aceps::os
