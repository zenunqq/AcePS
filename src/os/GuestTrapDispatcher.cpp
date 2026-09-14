/*
 * GuestTrapDispatcher.cpp keeps syscall policy independent of host trap
 * mechanics. Platform adapters only capture a verified frame and arrange to
 * resume guest execution; registry dispatch and logging occur in ordinary
 * process context, never inside a Linux signal handler.
 */
#include "aceps/os/GuestTrapDispatcher.h"

#include "aceps/common/Logging.h"

#include <array>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace aceps::os {
namespace {

constexpr std::uint32_t kSceKernelErrorEnosys = 0x80020016U;

[[nodiscard]] constexpr SyscallResult unknownSyscallResult() noexcept {
  return static_cast<SyscallResult>(static_cast<std::int32_t>(kSceKernelErrorEnosys));
}

[[nodiscard]] std::string describeFrame(const GuestRegisterFrame& frame) {
  std::ostringstream stream;
  stream << "SYSCALL #" << frame.syscallNumber() << " args=[" << std::hex << frame.arg(0) << ','
         << frame.arg(1) << ',' << frame.arg(2) << ',' << frame.arg(3) << ',' << frame.arg(4) << ','
         << frame.arg(5) << ']';
  return stream.str();
}

} // namespace

GuestTrapDispatcher::~GuestTrapDispatcher() { uninstall(); }

bool GuestTrapDispatcher::install(SyscallRegistry& registry, std::string& error) {
  std::scoped_lock lock(mutex_);
  if (installed_) {
    if (registry_ != &registry) {
      error = "guest trap dispatcher is already installed for a different syscall registry";
      return false;
    }
    error.clear();
    return true;
  }

  registry_ = &registry;
  if (!platformInstall(registry, error)) {
    registry_ = nullptr;
    return false;
  }

  installed_ = true;
  error.clear();
  return true;
}

void GuestTrapDispatcher::uninstall() noexcept {
  std::scoped_lock lock(mutex_);
  if (!installed_) return;
  platformUninstall();
  registry_ = nullptr;
  installed_ = false;
}

bool GuestTrapDispatcher::patchSite(void* instructionAddress, std::string& error) {
  std::scoped_lock lock(mutex_);
  if (!installed_) {
    error = "guest trap dispatcher must be installed before patching syscall sites";
    return false;
  }
  return platformPatchSite(instructionAddress, error);
}

bool GuestTrapDispatcher::installed() const noexcept {
  std::scoped_lock lock(mutex_);
  return installed_;
}

void GuestTrapDispatcher::dispatchFrame(GuestRegisterFrame& frame, SyscallRegistry& registry) noexcept {
  const auto number = frame.syscallNumber();
  if (number > std::numeric_limits<SyscallNumber>::max() ||
      !registry.contains(static_cast<SyscallNumber>(number))) {
    aceps::logging::warn("GuestTrapDispatcher: unrecognized " + describeFrame(frame));
    frame.setReturnValue(unknownSyscallResult());
    return;
  }

  const std::array<std::uint64_t, 6> rawArguments{
      frame.arg(0), frame.arg(1), frame.arg(2), frame.arg(3), frame.arg(4), frame.arg(5)};
  const std::string callDescription = describeFrame(frame);
  try {
    const SyscallResult result = registry.dispatch(
        static_cast<SyscallNumber>(number),
        std::vector<std::uint64_t>(rawArguments.begin(), rawArguments.end()));
    frame.setReturnValue(result);
    aceps::logging::debug("GuestTrapDispatcher: " + callDescription + " -> " +
                          std::to_string(static_cast<std::int64_t>(result)));
  } catch (...) {
    aceps::logging::error("GuestTrapDispatcher: syscall handler threw an exception");
    frame.setReturnValue(unknownSyscallResult());
  }
}

} // namespace aceps::os
