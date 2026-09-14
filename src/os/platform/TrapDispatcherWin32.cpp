/*
 * TrapDispatcherWin32.cpp implements the Windows/x86-64 vectored-exception
 * adapter for patched INT3 guest syscall sites. It handles only recorded sites
 * and returns CONTINUE_SEARCH for every unrelated host exception.
 */
#include "aceps/os/GuestTrapDispatcher.h"

#if defined(_WIN32) && defined(_M_X64)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace aceps::os {
namespace {

struct PatchSite final {
  std::uintptr_t address{0};
  DWORD restoreProtection{0};
};

struct PlatformState final {
  SyscallRegistry* registry{nullptr};
  void* vectoredHandler{nullptr};
  std::vector<PatchSite> patches;
};

std::atomic<PlatformState*> activeState{nullptr};
std::mutex stateMutex;
std::unique_ptr<PlatformState> installedState;
thread_local bool handlingTrap = false;

[[nodiscard]] bool hasPatchedSite(const PlatformState& state, const std::uintptr_t address) noexcept {
  for (const auto& patch : state.patches) {
    if (patch.address == address) return true;
  }
  return false;
}

void captureFrame(const CONTEXT& context, GuestRegisterFrame& frame) noexcept {
  frame.rax = context.Rax;
  frame.rbx = context.Rbx;
  frame.rcx = context.Rcx;
  frame.rdx = context.Rdx;
  frame.rsi = context.Rsi;
  frame.rdi = context.Rdi;
  frame.rbp = context.Rbp;
  frame.rsp = context.Rsp;
  frame.r8 = context.R8;
  frame.r9 = context.R9;
  frame.r10 = context.R10;
  frame.r11 = context.R11;
  frame.r12 = context.R12;
  frame.r13 = context.R13;
  frame.r14 = context.R14;
  frame.r15 = context.R15;
  frame.rip = context.Rip;
  frame.rflags = context.EFlags;
}

LONG CALLBACK vectoredHandler(PEXCEPTION_POINTERS exceptionInfo) noexcept {
  auto* state = activeState.load(std::memory_order_acquire);
  if (state == nullptr || exceptionInfo == nullptr || exceptionInfo->ExceptionRecord == nullptr ||
      exceptionInfo->ContextRecord == nullptr || handlingTrap) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (exceptionInfo->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  const auto trapAddress = reinterpret_cast<std::uintptr_t>(exceptionInfo->ExceptionRecord->ExceptionAddress);
  if (!hasPatchedSite(*state, trapAddress)) return EXCEPTION_CONTINUE_SEARCH;

  handlingTrap = true;
  GuestRegisterFrame frame{};
  captureFrame(*exceptionInfo->ContextRecord, frame);
  GuestTrapDispatcher::dispatchFrame(frame, *state->registry);
  exceptionInfo->ContextRecord->Rax = frame.rax;
  exceptionInfo->ContextRecord->Rip = static_cast<DWORD64>(trapAddress + 1U);
  handlingTrap = false;
  return EXCEPTION_CONTINUE_EXECUTION;
}

void restorePatchedSites(PlatformState& state) noexcept {
  for (auto it = state.patches.rbegin(); it != state.patches.rend(); ++it) {
    DWORD temporaryProtection = 0;
    auto* bytes = reinterpret_cast<std::uint8_t*>(it->address);
    if (VirtualProtect(bytes, 2U, PAGE_EXECUTE_READWRITE, &temporaryProtection) == 0) continue;
    bytes[0] = 0x0FU;
    bytes[1] = 0x05U;
    (void)FlushInstructionCache(GetCurrentProcess(), bytes, 2U);
    DWORD ignored = 0;
    (void)VirtualProtect(bytes, 2U, it->restoreProtection, &ignored);
  }
  state.patches.clear();
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
  state->vectoredHandler = AddVectoredExceptionHandler(1U, vectoredHandler);
  if (state->vectoredHandler == nullptr) {
    error = "AddVectoredExceptionHandler failed";
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

  activeState.store(nullptr, std::memory_order_release);
  restorePatchedSites(*installedState);
  if (installedState->vectoredHandler != nullptr) {
    (void)RemoveVectoredExceptionHandler(installedState->vectoredHandler);
  }
  installedState.reset();
}

bool GuestTrapDispatcher::platformPatchSite(void* instructionAddress, std::string& error) {
  std::scoped_lock lock(stateMutex);
  if (installedState == nullptr || instructionAddress == nullptr) {
    error = "guest trap dispatcher is not installed or patch address is null";
    return false;
  }

  const auto address = reinterpret_cast<std::uintptr_t>(instructionAddress);
  if (hasPatchedSite(*installedState, address)) {
    error.clear();
    return true;
  }
  MEMORY_BASIC_INFORMATION memoryInfo {};
  if (VirtualQuery(instructionAddress, &memoryInfo, sizeof(memoryInfo)) == 0 ||
      memoryInfo.State != MEM_COMMIT || memoryInfo.Protect == PAGE_NOACCESS) {
    error = "syscall patch site is not committed guest memory";
    return false;
  }
  const auto* existing = static_cast<const std::uint8_t*>(instructionAddress);
  if (existing[0] != 0x0FU || existing[1] != 0x05U) {
    error = "syscall patch site does not contain a SYSCALL instruction";
    return false;
  }

  DWORD originalProtection = 0;
  if (VirtualProtect(instructionAddress, 2U, PAGE_EXECUTE_READWRITE, &originalProtection) == 0) {
    error = "VirtualProtect could not make the guest syscall site writable";
    return false;
  }
  auto* bytes = static_cast<std::uint8_t*>(instructionAddress);
  bytes[0] = 0xCCU;
  bytes[1] = 0x90U;
  (void)FlushInstructionCache(GetCurrentProcess(), bytes, 2U);
  DWORD ignored = 0;
  if (VirtualProtect(instructionAddress, 2U, originalProtection, &ignored) == 0) {
    error = "guest syscall site was patched but its original protection could not be restored";
    return false;
  }
  installedState->patches.push_back(PatchSite{address, originalProtection});
  error.clear();
  return true;
}

} // namespace aceps::os

#elif defined(_WIN32)

namespace aceps::os {

bool GuestTrapDispatcher::platformInstall(SyscallRegistry&, std::string& error) {
  error = "guest trap dispatch requires Windows/x86-64";
  return false;
}

void GuestTrapDispatcher::platformUninstall() noexcept {}

bool GuestTrapDispatcher::platformPatchSite(void*, std::string& error) {
  error = "guest trap dispatch requires Windows/x86-64";
  return false;
}

} // namespace aceps::os

#endif
