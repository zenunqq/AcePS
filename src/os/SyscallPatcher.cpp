/*
 * SyscallPatcher.cpp redirects a faulting x86-64 syscall instruction into
 * SyscallRegistry. The platform callbacks do not own the registry and are
 * removed before the patcher is destroyed.
 */
#include "aceps/os/SyscallPatcher.h"

#include <array>
#include <cstdint>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__) && defined(__x86_64__)
#include <csignal>
#include <cstring>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace aceps::os {

struct SyscallPatcherState final {
  SyscallRegistry* registry{nullptr};
#if defined(_WIN32)
  void* vectoredHandler{nullptr};
#elif defined(__linux__) && defined(__x86_64__)
  struct sigaction previousSegv {};
  struct sigaction previousIll {};
#endif
};

namespace {

std::atomic<SyscallPatcherState*> activeState{nullptr};

bool isSyscallInstruction(const std::uint8_t* instructionPointer) noexcept {
  return instructionPointer != nullptr && instructionPointer[-2] == 0x0fU &&
         instructionPointer[-1] == 0x05U;
}

SyscallResult dispatch(SyscallPatcherState& state,
                       std::uint64_t number,
                       const std::array<std::uint64_t, 6>& arguments) noexcept {
  try {
    return state.registry->dispatch(static_cast<SyscallNumber>(number),
                                    std::vector<std::uint64_t>(arguments.begin(), arguments.end()));
  } catch (...) {
    return -1;
  }
}

#if defined(__linux__) && defined(__x86_64__)

void chainSignal(int signalNumber, siginfo_t* signalInfo, void* context,
                 const struct sigaction& previous) noexcept {
  if ((previous.sa_flags & SA_SIGINFO) != 0) {
    if (previous.sa_sigaction != nullptr) previous.sa_sigaction(signalNumber, signalInfo, context);
    return;
  }
  if (previous.sa_handler == SIG_IGN) return;
  if (previous.sa_handler != nullptr && previous.sa_handler != SIG_DFL) {
    previous.sa_handler(signalNumber);
    return;
  }
  std::signal(signalNumber, SIG_DFL);
  ::raise(signalNumber);
}

void signalHandler(int signalNumber, siginfo_t* signalInfo, void* context) noexcept {
  auto* state = activeState.load(std::memory_order_acquire);
  if (state == nullptr || context == nullptr) {
    return;
  }

  auto* machineContext = static_cast<ucontext_t*>(context);
  const auto instructionPointer = reinterpret_cast<const std::uint8_t*>(
      static_cast<std::uintptr_t>(machineContext->uc_mcontext.gregs[REG_RIP]));
  if (!isSyscallInstruction(instructionPointer)) {
    const auto& previous = signalNumber == SIGSEGV ? state->previousSegv : state->previousIll;
    chainSignal(signalNumber, signalInfo, context, previous);
    return;
  }

  const std::array<std::uint64_t, 6> arguments{
      static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RDI]),
      static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RSI]),
      static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RDX]),
      static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_R10]),
      static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_R8]),
      static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_R9])};
  const auto result = dispatch(*state,
                               static_cast<std::uint64_t>(machineContext->uc_mcontext.gregs[REG_RAX]),
                               arguments);
  machineContext->uc_mcontext.gregs[REG_RAX] = static_cast<greg_t>(result);
  machineContext->uc_mcontext.gregs[REG_RIP] += 2;
}

#endif

#if defined(_WIN32)

LONG CALLBACK vectoredHandler(PEXCEPTION_POINTERS exceptionInfo) noexcept {
  auto* state = activeState.load(std::memory_order_acquire);
  if (state == nullptr || exceptionInfo == nullptr || exceptionInfo->ContextRecord == nullptr) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const auto code = exceptionInfo->ExceptionRecord->ExceptionCode;
  if (code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  auto& context = *exceptionInfo->ContextRecord;
  auto* instructionPointer = reinterpret_cast<const std::uint8_t*>(context.Rip);
  if (context.Rip < 2 || !isSyscallInstruction(instructionPointer)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const std::array<std::uint64_t, 6> arguments{
      context.Rdi, context.Rsi, context.Rdx, context.R10, context.R8, context.R9};
  context.Rax = static_cast<DWORD64>(dispatch(*state, context.Rax, arguments));
  context.Rip += 2;
  return EXCEPTION_CONTINUE_EXECUTION;
}

#endif

} // namespace

SyscallPatcher::SyscallPatcher(SyscallRegistry& registry) noexcept : registry_(registry) {}

SyscallPatcher::~SyscallPatcher() {
  std::string ignored;
  (void)uninstall(ignored);
}

bool SyscallPatcher::install(std::string& error) {
  std::scoped_lock lock(mutex_);
  if (installed_.load(std::memory_order_acquire)) {
    error.clear();
    return true;
  }

#if defined(_WIN32)
  auto state = std::make_unique<SyscallPatcherState>();
  state->registry = &registry_;
  state->vectoredHandler = AddVectoredExceptionHandler(1, vectoredHandler);
  if (state->vectoredHandler == nullptr) {
    error = "AddVectoredExceptionHandler failed";
    return false;
  }
  state_ = std::move(state);
  activeState.store(state_.get(), std::memory_order_release);
#elif defined(__linux__) && defined(__x86_64__)
  auto state = std::make_unique<SyscallPatcherState>();
  state->registry = &registry_;
  struct sigaction action {};
  sigemptyset(&action.sa_mask);
  action.sa_sigaction = signalHandler;
  action.sa_flags = SA_SIGINFO;
  if (sigaction(SIGSEGV, &action, &state->previousSegv) != 0) {
    error = "sigaction(SIGSEGV) failed";
    return false;
  }
  if (sigaction(SIGILL, &action, &state->previousIll) != 0) {
    (void)sigaction(SIGSEGV, &state->previousSegv, nullptr);
    error = "sigaction(SIGILL) failed";
    return false;
  }
  state_ = std::move(state);
  activeState.store(state_.get(), std::memory_order_release);
#else
  error = "syscall patching is unsupported on this platform or architecture";
  return false;
#endif

  installed_.store(true, std::memory_order_release);
  error.clear();
  return true;
}

bool SyscallPatcher::uninstall(std::string& error) {
  std::scoped_lock lock(mutex_);
  if (!installed_.load(std::memory_order_acquire)) {
    error.clear();
    return true;
  }

  activeState.store(nullptr, std::memory_order_release);
  bool success = true;
#if defined(_WIN32)
  if (state_ == nullptr || RemoveVectoredExceptionHandler(state_->vectoredHandler) == 0) {
    error = "RemoveVectoredExceptionHandler failed";
    success = false;
  }
#elif defined(__linux__) && defined(__x86_64__)
  if (state_ == nullptr || sigaction(SIGSEGV, &state_->previousSegv, nullptr) != 0) {
    error = "sigaction(SIGSEGV) restore failed";
    success = false;
  }
  if (state_ == nullptr || sigaction(SIGILL, &state_->previousIll, nullptr) != 0) {
    if (error.empty()) error = "sigaction(SIGILL) restore failed";
    success = false;
  }
#else
  error = "syscall patching is unsupported on this platform or architecture";
  success = false;
#endif
  state_.reset();
  installed_.store(false, std::memory_order_release);
  if (success) error.clear();
  return success;
}

bool SyscallPatcher::installed() const noexcept {
  return installed_.load(std::memory_order_acquire);
}

} // namespace aceps::os
