/*
 * Kernel.cpp implements the first file, memory, process, and diagnostic HLE
 * calls needed by a small legal ELF test workload to survive startup.
 */
#include "aceps/os/Kernel.h"

#include "aceps/common/Logging.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace aceps::os {
namespace {

constexpr SyscallNumber kExit = 1;
constexpr SyscallNumber kRead = 3;
constexpr SyscallNumber kWrite = 4;
constexpr SyscallNumber kOpen = 5;
constexpr SyscallNumber kClose = 6;
constexpr SyscallNumber kGetPid = 20;
constexpr SyscallNumber kMmap = 477;
constexpr SyscallNumber kMunmap = 478;
constexpr SyscallNumber kPrintf = 572;
constexpr SyscallNumber kIsNeoMode = 615;
constexpr std::size_t kMaxPrintfLength = 4096;

SyscallResult errorResult() noexcept { return -static_cast<SyscallResult>(errno == 0 ? EIO : errno); }

bool argumentAvailable(const std::vector<std::uint64_t>& arguments, std::size_t count) noexcept {
  return arguments.size() >= count;
}

bool toSize(std::uint64_t value, std::size_t& result) noexcept {
  if (value > std::numeric_limits<std::size_t>::max()) return false;
  result = static_cast<std::size_t>(value);
  return true;
}

bool toFileDescriptor(std::uint64_t value, int& result) noexcept {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return false;
  result = static_cast<int>(value);
  return true;
}

bool guestString(std::uint64_t address, std::string& result) {
  if (address == 0) return false;
  const auto* text = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(address));
  std::size_t length = 0;
  while (length < kMaxPrintfLength && text[length] != '\0') ++length;
  if (length == kMaxPrintfLength) return false;
  result.assign(text, length);
  return true;
}

SyscallResult handleExit(const std::vector<std::uint64_t>& arguments) noexcept {
  std::exit(argumentAvailable(arguments, 1) ? static_cast<int>(arguments[0]) : 0);
}

SyscallResult handleMmap(memory::VirtualMemoryManager& memory,
                         const std::vector<std::uint64_t>& arguments) noexcept {
  if (!argumentAvailable(arguments, 2)) return -EINVAL;
  std::size_t size = 0;
  if (!toSize(arguments[1], size)) return -EINVAL;
  std::string error;
  void* address = memory.allocate(size, memory::Protection::ReadWrite, error);
  return address == nullptr ? errorResult() : static_cast<SyscallResult>(
      reinterpret_cast<std::uintptr_t>(address));
}

SyscallResult handleMunmap(memory::VirtualMemoryManager& memory,
                           const std::vector<std::uint64_t>& arguments) noexcept {
  if (!argumentAvailable(arguments, 2)) return -EINVAL;
  std::size_t size = 0;
  if (!toSize(arguments[1], size)) return -EINVAL;
  std::string error;
  return memory.release(reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[0])), size, error)
             ? 0
             : errorResult();
}

SyscallResult handleOpen(filesystem::VirtualFileSystem& fileSystem,
                         const std::vector<std::uint64_t>& arguments) noexcept {
  if (!argumentAvailable(arguments, 2)) return -EINVAL;
  std::string guestPath;
  if (!guestString(arguments[0], guestPath)) return -EFAULT;
  std::string error;
  const auto resolved = fileSystem.resolve(guestPath, error);
  if (!resolved.has_value()) return -ENOENT;
  const auto flags = static_cast<int>(arguments[1]);
  const auto mode = argumentAvailable(arguments, 3) ? static_cast<unsigned int>(arguments[2]) : 0U;
#if defined(_WIN32)
  const int descriptor = ::_open(resolved->string().c_str(), flags, mode);
#else
  const int descriptor = ::open(resolved->c_str(), flags, mode);
#endif
  return descriptor < 0 ? errorResult() : descriptor;
}

SyscallResult handleRead(const std::vector<std::uint64_t>& arguments) noexcept {
  if (!argumentAvailable(arguments, 3)) return -EINVAL;
  int descriptor = -1;
  std::size_t size = 0;
  if (!toFileDescriptor(arguments[0], descriptor) || !toSize(arguments[2], size)) return -EINVAL;
#if defined(_WIN32)
  const auto result = ::_read(descriptor, reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[1])),
                              static_cast<unsigned int>(size));
#else
  const auto result = ::read(descriptor, reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[1])), size);
#endif
  return result < 0 ? errorResult() : result;
}

SyscallResult handleWrite(const std::vector<std::uint64_t>& arguments) noexcept {
  if (!argumentAvailable(arguments, 3)) return -EINVAL;
  int descriptor = -1;
  std::size_t size = 0;
  if (!toFileDescriptor(arguments[0], descriptor) || !toSize(arguments[2], size)) return -EINVAL;
#if defined(_WIN32)
  const auto result = ::_write(descriptor, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(arguments[1])),
                               static_cast<unsigned int>(size));
#else
  const auto result = ::write(descriptor, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(arguments[1])), size);
#endif
  return result < 0 ? errorResult() : result;
}

SyscallResult handlePrintf(const std::vector<std::uint64_t>& arguments) noexcept {
  if (!argumentAvailable(arguments, 1)) return -EINVAL;
  std::string message;
  if (!guestString(arguments[0], message)) return -EFAULT;
  aceps::logging::info(message);
  return static_cast<SyscallResult>(message.size());
}

} // namespace

KernelSubsystem::KernelSubsystem(memory::VirtualMemoryManager& memory,
                                 filesystem::VirtualFileSystem& fileSystem) noexcept
    : memory_(memory), fileSystem_(fileSystem) {}

std::string_view KernelSubsystem::name() const noexcept { return "kernel"; }

bool KernelSubsystem::initialize(const core::ServiceContext&, std::string& error) {
  if (initialized_) {
    error.clear();
    return true;
  }
  if (registry_.size() == 10) {
    initialized_ = true;
    error.clear();
    return true;
  }

  const auto registerHandler = [this, &error](SyscallNumber number, SyscallHandler handler) {
    return registry_.registerHandler(number, std::move(handler), error);
  };
  if (!registerHandler(kExit, [](const auto& arguments) { return handleExit(arguments); }) ||
      !registerHandler(kMmap, [this](const auto& arguments) { return handleMmap(memory_, arguments); }) ||
      !registerHandler(kMunmap, [this](const auto& arguments) { return handleMunmap(memory_, arguments); }) ||
      !registerHandler(kOpen, [this](const auto& arguments) { return handleOpen(fileSystem_, arguments); }) ||
      !registerHandler(kRead, [](const auto& arguments) { return handleRead(arguments); }) ||
      !registerHandler(kClose, [](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        int descriptor = -1;
        if (!toFileDescriptor(arguments[0], descriptor)) return static_cast<SyscallResult>(-EINVAL);
#if defined(_WIN32)
        return static_cast<SyscallResult>(::_close(descriptor) == 0 ? 0 : errorResult());
#else
        return static_cast<SyscallResult>(::close(descriptor) == 0 ? 0 : errorResult());
#endif
      }) ||
      !registerHandler(kWrite, [](const auto& arguments) { return handleWrite(arguments); }) ||
      !registerHandler(kGetPid, [](const auto&) { return static_cast<SyscallResult>(1); }) ||
      !registerHandler(kPrintf, [](const auto& arguments) { return handlePrintf(arguments); }) ||
      !registerHandler(kIsNeoMode, [](const auto&) { return static_cast<SyscallResult>(0); })) {
    return false;
  }

  initialized_ = true;
  error.clear();
  return true;
}

void KernelSubsystem::shutdown() noexcept { initialized_ = false; }

SyscallRegistry& KernelSubsystem::registry() noexcept { return registry_; }

const SyscallRegistry& KernelSubsystem::registry() const noexcept { return registry_; }

} // namespace aceps::os
