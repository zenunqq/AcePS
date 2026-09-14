/*
 * SmokeTests.cpp exercises configuration persistence, path safety, and the
 * emulator lifecycle without requiring Qt, Vulkan, or game assets.
 */
#include "aceps/config/ApplicationConfig.h"
#include "aceps/config/PlatformPaths.h"
#include "aceps/app/GameLibrary.h"
#include "aceps/core/Emulator.h"
#include "aceps/core/EmulatorError.h"
#include "aceps/core/FrameClock.h"
#include "aceps/core/WorkScheduler.h"
#include "aceps/loader/SelfLoader.h"
#include "aceps/loader/ModuleRegistry.h"
#include "aceps/filesystem/AsyncFileService.h"
#include "aceps/gpu/GpuQueue.h"
#include "aceps/common/Profiler.h"
#include "aceps/filesystem/VirtualFileSystem.h"
#include "aceps/gpu/Pm4Parser.h"
#include "aceps/gpu/ContextTracker.h"
#include "aceps/memory/VirtualMemoryManager.h"
#include "aceps/os/GuestTrapDispatcher.h"
#include "aceps/os/SyscallPatcher.h"
#include "aceps/os/SyscallRegistry.h"

#include <cstdlib>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include <cstring>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}
} // namespace

int main() {
  const auto temporaryRoot = std::filesystem::temp_directory_path() / "aceps-smoke";
  std::filesystem::remove_all(temporaryRoot);
  std::filesystem::create_directories(temporaryRoot);

  const auto defaults = aceps::config::ApplicationConfig::defaults(temporaryRoot);
  std::string error;
  require(defaults.validate(error), "default configuration must validate");
  require(error.empty(), "valid configuration must clear the error");

  const auto configFile = temporaryRoot / "config.ini";
  require(defaults.save(configFile, error), "configuration must save");
  const auto loaded = aceps::config::ApplicationConfig::load(configFile, error);
  require(loaded.has_value(), "saved configuration must load");
  require(loaded->libraryDirectory == defaults.libraryDirectory, "library path must round-trip");
  require(loaded->graphics.vsync == defaults.graphics.vsync, "graphics settings must round-trip");

  auto invalid = defaults;
  invalid.graphics.resolutionScale = 5;
  require(!invalid.validate(error), "out-of-range scale must be rejected");
  require(!error.empty(), "invalid configuration must explain the error");

  aceps::filesystem::VirtualFileSystem vfs(temporaryRoot / "app", temporaryRoot / "save");
  const auto appFile = vfs.resolve("/app0/sce_sys/param.sfo", error);
  require(appFile.has_value(), "app0 path must resolve");
  require(appFile->filename() == "param.sfo", "resolved app path must retain filename");
  require(!vfs.resolve("/app0/../../escape", error).has_value(), "path traversal must be rejected");
  require(!vfs.resolve("/kernel/config", error).has_value(), "unknown namespace must be rejected");

  const auto gamesRoot = temporaryRoot / "games";
  std::filesystem::create_directories(gamesRoot / "Zeta" / "sce_sys");
  std::filesystem::create_directories(gamesRoot / "Alpha" / "sce_sys");
  std::filesystem::create_directories(gamesRoot / "NotAGame");
  aceps::app::GameLibrary library(gamesRoot);
  require(library.refresh(error), "game library scan must succeed");
  require(library.entries().size() == 2, "only directories with sce_sys are cataloged");
  require(library.entries().front().displayName == "Alpha", "library entries are sorted");

  aceps::memory::VirtualMemoryManager memory;
  void* allocation = memory.allocate(1, aceps::memory::Protection::ReadWrite, error);
  require(allocation != nullptr, "host memory allocation must succeed");
  require(memory.allocationCount() == 1, "allocation must be tracked");
  require(memory.allocatedBytes() == memory.pageSize(), "allocation bytes must be page-rounded");
  require(memory.protect(allocation, memory.pageSize(), aceps::memory::Protection::Read, error),
          "owned allocation protection must succeed");
  require(memory.release(allocation, memory.pageSize(), error), "owned allocation release must succeed");
  require(memory.allocationCount() == 0, "release must remove allocation tracking");
  require(memory.allocatedBytes() == 0, "release must clear byte accounting");

  aceps::os::SyscallRegistry syscalls;
  require(syscalls.registerHandler(42, [](const std::vector<std::uint64_t>& args) {
            return static_cast<std::int64_t>(args.size());
          }, error), "syscall handler registration must succeed");
  require(syscalls.dispatch(42, {1, 2, 3}) == 3, "registered syscall must dispatch");
  require(syscalls.dispatch(999, {}) < 0, "unknown syscall must return an error");

  std::vector<aceps::gpu::Pm4Packet> packets;
  const std::vector<std::uint32_t> commandBuffer{0xC0C00000U, 0xDEADBEEFU};
  require(aceps::gpu::Pm4Parser::parse(commandBuffer, packets, error), "valid PM4 must parse");
  require(packets.size() == 1 && packets.front().opcode == 0xC0U, "PM4 opcode must decode");
  require(!aceps::gpu::Pm4Parser::parse({0xC0C00001U, 0xDEADBEEFU}, packets, error), "truncated PM4 must fail");
  std::vector<aceps::gpu::Pm4PacketView> views;
  require(aceps::gpu::Pm4Parser::parseViews(commandBuffer, views, error), "zero-copy PM4 must parse");
  require(views.size() == 1 && views.front().payload.data() == commandBuffer.data() + 1,
          "PM4 view must borrow command-buffer storage");
  require(views.front().payload.front() == 0xDEADBEEFU, "PM4 view payload must be readable");

  require(syscalls.dispatchCount() == 2, "syscall statistics must count known and unknown dispatches");

  aceps::gpu::ContextTracker context;
  context.setContextReg(0xA318U, 0x100U);
  context.setContextReg(0xA319U, 7U);
  context.setContextReg(0xA31BU, (640U << 16U) | 480U);
  require(context.renderTarget(0).baseAddr == 0x10000U, "context tracker must decode color base address");
  require(context.renderTarget(0).width == 641U && context.renderTarget(0).height == 481U,
          "context tracker must decode color dimensions");
  context.setShReg(0x2C8U, 0x1234U);
  context.setShReg(0x2C9U, 0x5678U);
  require(context.vertexShaderAddr() == 0x567800001234ULL,
          "context tracker must decode vertex shader address");
  require(context.dirty(), "context tracker must mark changed state dirty");
  context.clearDirty();
  require(!context.dirty(), "context tracker must clear dirty state");

  aceps::os::GuestRegisterFrame frame{};
  frame.rax = 42;
  frame.rdi = 1;
  frame.rsi = 2;
  frame.rdx = 3;
  frame.r10 = 4;
  frame.r8 = 5;
  frame.r9 = 6;
  aceps::os::GuestTrapDispatcher::dispatchFrame(frame, syscalls);
  require(static_cast<std::int64_t>(frame.rax) == 6, "register-frame dispatch must call the syscall registry");
  require(syscalls.dispatchCount() == 3, "register-frame dispatch must update registry metrics");

  frame.rax = 0x1'0000'0000ULL;
  aceps::os::GuestTrapDispatcher::dispatchFrame(frame, syscalls);
  require(static_cast<std::uint32_t>(frame.rax) == 0x80020016U,
          "unrecognized guest syscall must return the Orbis ENOSYS error");

  std::array<std::uint8_t, 7> syscallBytes{0x90U, 0x0FU, 0x05U, 0x0FU, 0x05U, 0x90U, 0x05U};
  const auto syscallSites = aceps::os::SyscallPatcher::scan(syscallBytes.data(), syscallBytes.size());
  require(syscallSites.size() == 2, "syscall scanner must identify each complete SYSCALL instruction");
  require(syscallSites[0] == syscallBytes.data() + 1 && syscallSites[1] == syscallBytes.data() + 3,
          "syscall scanner must return instruction addresses");

#if defined(__linux__) && defined(__x86_64__)
  aceps::memory::VirtualMemoryManager trapMemory;
  void* trapCode = trapMemory.allocate(trapMemory.pageSize(), aceps::memory::Protection::ReadWriteExecute, error);
  require(trapCode != nullptr, "trap integration code allocation must succeed");
  const std::array<std::uint8_t, 37> trapProgram{
      0x48U, 0xC7U, 0xC0U, 0x5AU, 0x00U, 0x00U, 0x00U, // mov rax, 90
      0x48U, 0xC7U, 0xC7U, 0x01U, 0x00U, 0x00U, 0x00U, // mov rdi, 1
      0x48U, 0xC7U, 0xC6U, 0x02U, 0x00U, 0x00U, 0x00U, // mov rsi, 2
      0x48U, 0xC7U, 0xC2U, 0x03U, 0x00U, 0x00U, 0x00U, // mov rdx, 3
      0x41U, 0xBAU, 0x04U, 0x00U, 0x00U, 0x00U,       // mov r10d, 4
      0x0FU, 0x05U,                                     // syscall
      0xC3U};                                           // ret
  static_assert(trapProgram.size() == 37U);
  std::memcpy(trapCode, trapProgram.data(), trapProgram.size());
  require(syscalls.registerHandler(90, [](const std::vector<std::uint64_t>& arguments) {
            return static_cast<std::int64_t>(arguments[0] + arguments[1] + arguments[2] + arguments[3]);
          }, error), "trap integration syscall handler must register");
  aceps::os::GuestTrapDispatcher dispatcher;
  require(dispatcher.install(syscalls, error), "guest trap dispatcher must install on Linux/x86-64");
  const auto trapSites = aceps::os::SyscallPatcher::scan(trapCode, trapProgram.size());
  require(trapSites.size() == 1, "guest trap integration program must contain one SYSCALL instruction");
  require(dispatcher.patchSite(trapSites.front(), error), "guest trap dispatcher must patch the SYSCALL instruction");
  using TrapEntryPoint = std::int64_t (*)();
  const auto trapEntry = reinterpret_cast<TrapEntryPoint>(trapCode);
  require(trapEntry() == 10, "patched guest SYSCALL must dispatch and resume with the return value");
  dispatcher.uninstall();
  const auto* restoredTrapBytes = static_cast<const std::uint8_t*>(trapCode);
  require(restoredTrapBytes[34] == 0x0FU && restoredTrapBytes[35] == 0x05U,
          "dispatcher teardown must restore each patched SYSCALL instruction");
  require(trapMemory.release(trapCode, trapMemory.pageSize(), error), "trap integration code must release");
#endif

  aceps::core::Emulator emulator(defaults);
  require(emulator.state() == aceps::core::EmulatorState::Created, "emulator starts in Created");
  emulator.initialize();
  require(emulator.isInitialized(), "emulator enters Running");
  require(emulator.initializedSubsystemCount() == 5, "all starter services initialize");
  emulator.initialize();
  require(emulator.initializedSubsystemCount() == 5, "initialize is idempotent");
  emulator.shutdown();
  require(emulator.state() == aceps::core::EmulatorState::Stopped, "emulator enters Stopped");
  require(emulator.initializedSubsystemCount() == 0, "shutdown clears services");

  bool threw = false;
  try {
    aceps::core::Emulator broken(invalid);
    broken.initialize();
  } catch (const aceps::core::EmulatorError&) {
    threw = true;
  }
  require(threw, "invalid configuration must throw a typed emulator error");

  aceps::core::WorkScheduler scheduler(2, 8);
  auto first = scheduler.submit([] { return 20U + 22U; });
  auto second = scheduler.submit([] { return std::string("scheduled"); });
  require(first.get() == 42U, "scheduled numeric work must complete");
  require(second.get() == "scheduled", "scheduled string work must complete");
  require(scheduler.workerCount() == 2, "scheduler worker count must be configured");
  scheduler.shutdown();

  bool submitAfterShutdownThrew = false;
  try {
    (void)scheduler.submit([] {});
  } catch (const std::runtime_error&) {
    submitAfterShutdownThrew = true;
  }
  require(submitAfterShutdownThrew, "scheduler must reject work after shutdown");

  aceps::core::FrameClock clock(1000.0);
  clock.waitForNextFrame();
  require(clock.frameCount() == 1, "frame clock must count completed frames");
  require(clock.lastDelta().count() > 0, "frame clock must report a positive delta");

  std::ifstream hostBinary("/bin/sh", std::ios::binary);
  std::vector<std::uint8_t> elfImage((std::istreambuf_iterator<char>(hostBinary)), {});
  if (!elfImage.empty()) {
    aceps::loader::ElfLoadPlan loadPlan;
    require(aceps::loader::Elf64Loader::inspect(elfImage, loadPlan, error),
            "valid host ELF64 binary must produce a load plan");
    require(!loadPlan.segments.empty(), "ELF load plan must contain segments");
  }
  aceps::loader::ElfLoadPlan malformedPlan;
  const std::array<std::uint8_t, 4> malformedImage{0x7FU, 0x45U, 0x4CU, 0x46U};
  require(!aceps::loader::Elf64Loader::inspect(malformedImage, malformedPlan, error),
          "truncated ELF must be rejected");

  aceps::loader::ModuleRegistry modules;
  require(modules.registerModule("libkernel.sprx", error), "module registration must succeed");
  require(modules.registerExport("libkernel.sprx", "sceKernelGettimeofday", 0x1000, error),
          "module export registration must succeed");
  aceps::loader::GuestAddress symbolAddress = 0;
  require(modules.resolve("libkernel.sprx", "sceKernelGettimeofday", symbolAddress, error) &&
              symbolAddress == 0x1000,
          "module export must resolve to its guest address");

  const auto asset = temporaryRoot / "asset.bin";
  { std::ofstream output(asset, std::ios::binary); output << "AcePS asset"; }
  aceps::filesystem::AsyncFileService fileService(temporaryRoot, 1);
  auto readFuture = fileService.read("asset.bin", 0, 11);
  const auto readResult = readFuture.get();
  require(readResult.succeeded() && readResult.bytes.size() == 11, "async file read must complete");
  require(fileService.read("asset.bin", 0, 11).get().succeeded(), "cached async file read must complete");
  require(fileService.cacheHits() == 1 && fileService.cacheEntries() == 1, "file cache must track bounded hits");
  require(!fileService.read("../escape", 0, 1).get().succeeded(), "async path traversal must fail");
  fileService.shutdown();

  aceps::gpu::GpuQueue gpuQueue(1);
  const auto fence = gpuQueue.submit([] {});
  require(gpuQueue.wait(fence), "GPU fence must wait for submitted work");
  require(gpuQueue.wait(fence), "completed GPU fence must remain idempotently waitable");
  require(gpuQueue.completedFence() >= fence, "GPU completion must advance the fence");

  aceps::gpu::GpuQueue concurrentGpuQueue(2);
  std::promise<void> firstStarted;
  auto firstStartedFuture = firstStarted.get_future().share();
  std::promise<void> releaseFirst;
  auto releaseFirstFuture = releaseFirst.get_future().share();
  const auto firstFence = concurrentGpuQueue.submit([&firstStarted, releaseFirstFuture] {
    firstStarted.set_value();
    releaseFirstFuture.wait();
  });
  firstStartedFuture.wait();
  const auto secondFence = concurrentGpuQueue.submit([] {});
  require(concurrentGpuQueue.wait(secondFence), "later GPU fence must be independently waitable");
  require(concurrentGpuQueue.completedFence() < secondFence,
          "out-of-order GPU completion must not skip earlier fences");
  releaseFirst.set_value();
  require(concurrentGpuQueue.wait(firstFence), "earlier GPU fence must complete after release");
  require(concurrentGpuQueue.completedFence() == secondFence,
          "ordered GPU completion must advance through contiguous fences");
  concurrentGpuQueue.shutdown();

  aceps::common::ProfileCounter profile;
  { aceps::common::ProfileScope scope(profile); }
  require(profile.snapshot().events == 1, "profile scope must record one event");
  std::filesystem::remove_all(temporaryRoot);
  return 0;
}
