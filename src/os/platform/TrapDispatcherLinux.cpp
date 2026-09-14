/*
 * TrapDispatcherLinux.cpp implements a SIGTRAP-based bridge for patched INT3
 * instructions on Linux/x86-64. INT3 delivers SIGTRAP (not SIGILL or SIGSEGV),
 * so unrelated SIGILL/SIGSEGV faults are deliberately left to their existing
 * handlers. The signal handler only captures registers and waits on a fixed,
 * preallocated request slot; registry dispatch and logging run on a worker.
 */
#include "aceps/os/GuestTrapDispatcher.h"

#if defined(__linux__) && defined(__x86_64__)

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <new>
#include <limits>
#include <poll.h>
#include <signal.h>
#include <string>
#include <thread>
#include <ucontext.h>
#include <unistd.h>

#include <sys/mman.h>

namespace aceps::os {
namespace {

constexpr std::size_t kAlternateStackBytes = 64U * 1024U;
constexpr std::size_t kMaximumPatchSites = 65536U;
constexpr std::size_t kMaximumPendingTraps = 64U;
constexpr std::uint32_t kRequestFree = 0U;
constexpr std::uint32_t kRequestFilling = 1U;
constexpr std::uint32_t kRequestPending = 2U;
constexpr std::uint32_t kRequestComplete = 3U;
constexpr std::uint32_t kSceKernelErrorEnosys = 0x80020016U;

static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(std::atomic<std::size_t>::is_always_lock_free);
static_assert(std::atomic<void*>::is_always_lock_free);

struct TrapRequest final {
  std::atomic<std::uint32_t> state{kRequestFree};
  GuestRegisterFrame frame{};
};

struct PatchSite final {
  std::uintptr_t address{0};
  std::uintptr_t pageStart{0};
  std::size_t pageLength{0};
  int restoreProtection{0};
};

struct PlatformState final {
  SyscallRegistry* registry{nullptr};
  struct sigaction previousTrap {};
  stack_t previousAlternateStack {};
  std::unique_ptr<std::byte[]> alternateStack;
  int wakeRead{-1};
  int wakeWrite{-1};
  std::thread worker;
  std::atomic<bool> stopping{false};
  std::array<TrapRequest, kMaximumPendingTraps> requests{};
  std::unique_ptr<PatchSite[]> patches{std::make_unique<PatchSite[]>(kMaximumPatchSites)};
  std::atomic<std::size_t> patchCount{0};
  std::atomic<std::size_t> droppedTrapCount{0};
};

std::atomic<PlatformState*> activeState{nullptr};
std::mutex stateMutex;
std::unique_ptr<PlatformState> installedState;
thread_local volatile sig_atomic_t handlingTrap = 0;

[[nodiscard]] constexpr SyscallResult unavailableResult() noexcept {
  return static_cast<SyscallResult>(static_cast<std::int32_t>(kSceKernelErrorEnosys));
}

[[nodiscard]] bool hasPatchedSite(const PlatformState& state, const std::uintptr_t address) noexcept {
  const auto count = state.patchCount.load(std::memory_order_acquire);
  for (std::size_t index = 0; index < count; ++index) {
    if (state.patches[index].address == address) return true;
  }
  return false;
}

[[nodiscard]] TrapRequest* acquireRequest(PlatformState& state) noexcept {
  for (auto& request : state.requests) {
    std::uint32_t expected = kRequestFree;
    if (request.state.compare_exchange_strong(expected, kRequestFilling, std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
      return &request;
    }
  }
  return nullptr;
}

void dispatchPendingRequests(PlatformState& state) noexcept {
  for (auto& request : state.requests) {
    if (request.state.load(std::memory_order_acquire) != kRequestPending) continue;
    GuestTrapDispatcher::dispatchFrame(request.frame, *state.registry);
    request.state.store(kRequestComplete, std::memory_order_release);
  }
}

void dispatcherWorker(PlatformState* state) noexcept {
  while (!state->stopping.load(std::memory_order_acquire)) {
    struct pollfd descriptor {};
    descriptor.fd = state->wakeRead;
    descriptor.events = POLLIN;
    const int pollResult = ::poll(&descriptor, 1, 100);
    if (pollResult > 0 && (descriptor.revents & POLLIN) != 0) {
      std::array<std::uint8_t, 128> notifications{};
      while (::read(state->wakeRead, notifications.data(), notifications.size()) > 0) {
      }
    }
    dispatchPendingRequests(*state);
  }
  dispatchPendingRequests(*state);
}

void restoreDefaultTrap(const struct sigaction& previous) noexcept {
  // The default path is intentionally a process-fatal re-delivery. A handler
  // cannot safely pretend that an unrecognized SIGTRAP was handled.
  (void)::sigaction(SIGTRAP, &previous, nullptr);
  (void)::kill(::getpid(), SIGTRAP);
}

void forwardTrap(int signalNumber, siginfo_t* signalInfo, void* context,
                 const struct sigaction& previous) noexcept {
  if ((previous.sa_flags & SA_SIGINFO) != 0 && previous.sa_sigaction != nullptr) {
    previous.sa_sigaction(signalNumber, signalInfo, context);
    return;
  }
  if (previous.sa_handler == SIG_IGN) return;
  if (previous.sa_handler != nullptr && previous.sa_handler != SIG_DFL) {
    previous.sa_handler(signalNumber);
    return;
  }
  restoreDefaultTrap(previous);
}

void signalHandler(int signalNumber, siginfo_t* signalInfo, void* context) noexcept {
  auto* state = activeState.load(std::memory_order_acquire);
  if (state == nullptr || context == nullptr || handlingTrap != 0) {
    if (state != nullptr) forwardTrap(signalNumber, signalInfo, context, state->previousTrap);
    return;
  }

  auto* machineContext = static_cast<ucontext_t*>(context);
  const auto instructionAfterTrap = static_cast<std::uintptr_t>(machineContext->uc_mcontext.gregs[REG_RIP]);
  if (instructionAfterTrap == 0 || !hasPatchedSite(*state, instructionAfterTrap - 1U)) {
    forwardTrap(signalNumber, signalInfo, context, state->previousTrap);
    return;
  }

  handlingTrap = 1;
  TrapRequest* request = acquireRequest(*state);
  if (request == nullptr) {
    state->droppedTrapCount.fetch_add(1, std::memory_order_relaxed);
    machineContext->uc_mcontext.gregs[REG_RAX] = static_cast<greg_t>(unavailableResult());
    handlingTrap = 0;
    return;
  }

  auto& frame = request->frame;
  frame.rax = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RAX]);
  frame.rbx = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RBX]);
  frame.rcx = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RCX]);
  frame.rdx = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RDX]);
  frame.rsi = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RSI]);
  frame.rdi = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RDI]);
  frame.rbp = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RBP]);
  frame.rsp = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RSP]);
  frame.r8 = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_R8]);
  frame.r9 = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_R9]);
  frame.r10 = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_R10]);
  frame.r11 = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_R11]);
  frame.r12 = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_R12]);
  frame.r13 = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_R13]);
  frame.r14 = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_R14]);
  frame.r15 = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_R15]);
  frame.rip = instructionAfterTrap;
  frame.rflags = static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_EFL]);

  request->state.store(kRequestPending, std::memory_order_release);
  const std::uint8_t notification = 1U;
  const auto writeResult = ::write(state->wakeWrite, &notification, sizeof(notification));
  (void)writeResult;

  while (request->state.load(std::memory_order_acquire) != kRequestComplete) {
  }

  machineContext->uc_mcontext.gregs[REG_RAX] = static_cast<greg_t>(frame.rax);
  // SIGTRAP reports RIP after INT3 already; the replacement NOP at RIP is the
  // consumed second byte of the original SYSCALL instruction.
  request->state.store(kRequestFree, std::memory_order_release);
  handlingTrap = 0;
}

[[nodiscard]] bool mappingProtection(const std::uintptr_t address, int& protection) {
  std::FILE* maps = std::fopen("/proc/self/maps", "r");
  if (maps == nullptr) return false;

  bool found = false;
  char line[512]{};
  while (std::fgets(line, sizeof(line), maps) != nullptr) {
    unsigned long begin = 0;
    unsigned long end = 0;
    char permissions[5]{};
    if (std::sscanf(line, "%lx-%lx %4s", &begin, &end, permissions) != 3) continue;
    if (address < static_cast<std::uintptr_t>(begin) || address >= static_cast<std::uintptr_t>(end)) continue;

    protection = 0;
    if (permissions[0] == 'r') protection |= PROT_READ;
    if (permissions[1] == 'w') protection |= PROT_WRITE;
    if (permissions[2] == 'x') protection |= PROT_EXEC;
    found = true;
    break;
  }
  (void)std::fclose(maps);
  return found;
}

[[nodiscard]] bool patchMemory(PlatformState& state, void* instructionAddress, std::string& error) {
  const auto address = reinterpret_cast<std::uintptr_t>(instructionAddress);
  if (address > std::numeric_limits<std::uintptr_t>::max() - 1U) {
    error = "syscall instruction address overflows";
    return false;
  }
  if (hasPatchedSite(state, address)) {
    error.clear();
    return true;
  }

  int firstProtection = 0;
  int secondProtection = 0;
  if (!mappingProtection(address, firstProtection) || !mappingProtection(address + 1U, secondProtection) ||
      firstProtection != secondProtection || (firstProtection & PROT_EXEC) == 0) {
    error = "syscall patch site is not within one executable guest mapping";
    return false;
  }
  const auto* existing = static_cast<const std::uint8_t*>(instructionAddress);
  if (existing[0] != 0x0FU || existing[1] != 0x05U) {
    error = "syscall patch site does not contain a SYSCALL instruction";
    return false;
  }

  const long pageSize = ::sysconf(_SC_PAGESIZE);
  if (pageSize <= 0) {
    error = "could not determine host page size";
    return false;
  }
  const auto page = static_cast<std::uintptr_t>(pageSize);
  const auto pageStart = address - (address % page);
  const auto pageEnd = ((address + 2U + page - 1U) / page) * page;
  if (pageEnd <= pageStart) {
    error = "syscall patch range overflows";
    return false;
  }
  const auto pageLength = static_cast<std::size_t>(pageEnd - pageStart);
  if (::mprotect(reinterpret_cast<void*>(pageStart), pageLength, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
    error = "mprotect could not make the guest syscall site writable";
    return false;
  }

  auto* bytes = static_cast<std::uint8_t*>(instructionAddress);
  bytes[0] = 0xCCU;
  bytes[1] = 0x90U;
  __builtin___clear_cache(reinterpret_cast<char*>(bytes), reinterpret_cast<char*>(bytes + 2));
  const bool restored = ::mprotect(reinterpret_cast<void*>(pageStart), pageLength, firstProtection) == 0;

  const auto count = state.patchCount.load(std::memory_order_relaxed);
  if (count >= kMaximumPatchSites) {
    bytes[0] = 0x0FU;
    bytes[1] = 0x05U;
    __builtin___clear_cache(reinterpret_cast<char*>(bytes), reinterpret_cast<char*>(bytes + 2));
    (void)::mprotect(reinterpret_cast<void*>(pageStart), pageLength, firstProtection);
    error = "guest executable contains too many syscall trap sites";
    return false;
  }
  state.patches[count] = PatchSite{address, pageStart, pageLength, firstProtection};
  state.patchCount.store(count + 1U, std::memory_order_release);

  if (!restored) {
    error = "guest syscall site was patched but its original protection could not be restored";
    return false;
  }
  error.clear();
  return true;
}

void restorePatchedSites(PlatformState& state) noexcept {
  const auto count = state.patchCount.exchange(0, std::memory_order_acq_rel);
  for (std::size_t index = count; index > 0; --index) {
    const auto& patch = state.patches[index - 1U];
    if (patch.address == 0 || patch.pageLength == 0) continue;
    if (::mprotect(reinterpret_cast<void*>(patch.pageStart), patch.pageLength,
                   PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
      continue;
    }
    auto* bytes = reinterpret_cast<std::uint8_t*>(patch.address);
    bytes[0] = 0x0FU;
    bytes[1] = 0x05U;
    __builtin___clear_cache(reinterpret_cast<char*>(bytes), reinterpret_cast<char*>(bytes + 2));
    (void)::mprotect(reinterpret_cast<void*>(patch.pageStart), patch.pageLength, patch.restoreProtection);
  }
}

void closeDescriptor(int& descriptor) noexcept {
  if (descriptor >= 0) (void)::close(descriptor);
  descriptor = -1;
}

} // namespace

bool GuestTrapDispatcher::platformInstall(SyscallRegistry& registry, std::string& error) {
  std::scoped_lock lock(stateMutex);
  if (installedState != nullptr) {
    error = "a guest trap dispatcher is already installed in this process";
    return false;
  }

  auto state = std::make_unique<PlatformState>();
  state->registry = &registry;
  state->alternateStack = std::make_unique<std::byte[]>(kAlternateStackBytes);
  int descriptors[2]{-1, -1};
  if (::pipe(descriptors) != 0 ||
      ::fcntl(descriptors[0], F_SETFL, O_NONBLOCK) != 0 ||
      ::fcntl(descriptors[1], F_SETFL, O_NONBLOCK) != 0 ||
      ::fcntl(descriptors[0], F_SETFD, FD_CLOEXEC) != 0 ||
      ::fcntl(descriptors[1], F_SETFD, FD_CLOEXEC) != 0) {
    if (descriptors[0] >= 0) (void)::close(descriptors[0]);
    if (descriptors[1] >= 0) (void)::close(descriptors[1]);
    error = "could not create guest trap dispatcher wake pipe";
    return false;
  }
  state->wakeRead = descriptors[0];
  state->wakeWrite = descriptors[1];

  stack_t alternateStack {};
  alternateStack.ss_sp = state->alternateStack.get();
  alternateStack.ss_size = kAlternateStackBytes;
  alternateStack.ss_flags = 0;
  if (::sigaltstack(&alternateStack, &state->previousAlternateStack) != 0) {
    closeDescriptor(state->wakeRead);
    closeDescriptor(state->wakeWrite);
    error = "sigaltstack installation failed";
    return false;
  }

  try {
    state->worker = std::thread(dispatcherWorker, state.get());
  } catch (...) {
    (void)::sigaltstack(&state->previousAlternateStack, nullptr);
    closeDescriptor(state->wakeRead);
    closeDescriptor(state->wakeWrite);
    error = "could not start guest trap dispatcher worker";
    return false;
  }

  struct sigaction action {};
  sigemptyset(&action.sa_mask);
  action.sa_sigaction = signalHandler;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK;
  if (::sigaction(SIGTRAP, &action, &state->previousTrap) != 0) {
    state->stopping.store(true, std::memory_order_release);
    const std::uint8_t notification = 1U;
    (void)::write(state->wakeWrite, &notification, sizeof(notification));
    state->worker.join();
    (void)::sigaltstack(&state->previousAlternateStack, nullptr);
    closeDescriptor(state->wakeRead);
    closeDescriptor(state->wakeWrite);
    error = "sigaction(SIGTRAP) installation failed";
    return false;
  }

  activeState.store(state.get(), std::memory_order_release);
  installedState = std::move(state);
  error.clear();
  return true;
}

void GuestTrapDispatcher::platformUninstall() noexcept {
  std::scoped_lock lock(stateMutex);
  if (installedState == nullptr) return;

  auto& state = *installedState;
  activeState.store(nullptr, std::memory_order_release);
  (void)::sigaction(SIGTRAP, &state.previousTrap, nullptr);
  state.stopping.store(true, std::memory_order_release);
  const std::uint8_t notification = 1U;
  (void)::write(state.wakeWrite, &notification, sizeof(notification));
  if (state.worker.joinable()) state.worker.join();
  restorePatchedSites(state);
  (void)::sigaltstack(&state.previousAlternateStack, nullptr);
  closeDescriptor(state.wakeRead);
  closeDescriptor(state.wakeWrite);
  installedState.reset();
}

bool GuestTrapDispatcher::platformPatchSite(void* instructionAddress, std::string& error) {
  std::scoped_lock lock(stateMutex);
  if (installedState == nullptr) {
    error = "guest trap dispatcher is not installed";
    return false;
  }
  return patchMemory(*installedState, instructionAddress, error);
}

} // namespace aceps::os

#else

namespace aceps::os {

bool GuestTrapDispatcher::platformInstall(SyscallRegistry&, std::string& error) {
  error = "guest trap dispatch is supported only on Linux/x86-64 and Windows/x86-64";
  return false;
}

void GuestTrapDispatcher::platformUninstall() noexcept {}

bool GuestTrapDispatcher::platformPatchSite(void*, std::string& error) {
  error = "guest trap dispatch is unsupported on this platform or architecture";
  return false;
}

} // namespace aceps::os

#endif
