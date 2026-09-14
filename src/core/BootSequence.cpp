/*
 * BootSequence.cpp connects ELF inspection, memory mapping, kernel HLE,
 * syscall interception, and entry-point transfer in one transactional path.
 */
#include "aceps/core/BootSequence.h"

#include "aceps/common/Logging.h"
#include "aceps/filesystem/VirtualFileSystem.h"
#include "aceps/gpu/CommandProcessor.h"
#include "aceps/loader/ElfMapper.h"
#include "aceps/loader/SelfLoader.h"
#include "aceps/memory/VirtualMemoryManager.h"
#include "aceps/os/GuestTrapDispatcher.h"
#include "aceps/os/Kernel.h"
#include "aceps/os/SyscallPatcher.h"

#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace aceps::core {
namespace {

void logStep(const std::string& message) { aceps::logging::info("[BootSequence] " + message); }

bool fail(std::string& error, const std::string& message) {
  error = message;
  aceps::logging::error("[BootSequence] " + message);
  return false;
}

bool readImage(const std::filesystem::path& path,
               std::vector<std::uint8_t>& image,
               std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return fail(error, "could not open ELF file: " + path.string());
  input.seekg(0, std::ios::end);
  const auto length = input.tellg();
  if (length < 0) return fail(error, "could not determine ELF file size: " + path.string());
  input.seekg(0, std::ios::beg);
  image.resize(static_cast<std::size_t>(length));
  if (!image.empty()) {
    input.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!input) return fail(error, "could not read ELF file: " + path.string());
  }
  return true;
}

} // namespace

bool BootSequence::run(const std::filesystem::path& elfPath, std::string& error,
                       const gpu::NativeWindowHandle* windowHandle) const {
  error.clear();
  logStep("starting boot for " + elfPath.string());

  std::vector<std::uint8_t> image;
  logStep("reading ELF image");
  if (!readImage(elfPath, image, error)) return false;
  logStep("read " + std::to_string(image.size()) + " bytes");

  loader::ElfLoadPlan plan{};
  logStep("inspecting ELF headers");
  if (!loader::Elf64Loader::inspect(image, plan, error)) {
    return fail(error, "ELF inspection failed: " + error);
  }
  logStep("ELF inspection succeeded with " + std::to_string(plan.segments.size()) + " segments");

  memory::VirtualMemoryManager memory;
  logStep("created virtual memory manager");

  const auto applicationRoot = std::filesystem::absolute(elfPath).parent_path();
  filesystem::VirtualFileSystem fileSystem(applicationRoot, applicationRoot / "savedata");
  logStep("created virtual filesystem with /app0 at " + applicationRoot.string());

  gpu::CommandProcessor commandProcessor;
  os::KernelSubsystem kernel(memory, fileSystem, &commandProcessor);
  logStep("initializing kernel subsystem");
  const ServiceContext context{"boot"};
  if (!kernel.initialize(context, error)) {
    return fail(error, "kernel initialization failed: " + error);
  }
  logStep("kernel subsystem initialized with " + std::to_string(kernel.registry().size()) +
          " syscall handlers");

  os::GuestTrapDispatcher dispatcher;
  logStep("installing guest trap dispatcher");
  if (!dispatcher.install(kernel.registry(), error)) {
    kernel.shutdown();
    return fail(error, "guest trap dispatcher installation failed: " + error);
  }
  logStep("guest trap dispatcher installed");

  loader::ElfMapper mapper;
  logStep("mapping ELF segments into host memory");
  if (!mapper.map(image, plan, memory, error)) {
    dispatcher.uninstall();
    kernel.shutdown();
    return fail(error, "ELF mapping failed: " + error);
  }
  logStep("mapped " + std::to_string(mapper.mappingCount()) + " ELF segments");

  std::size_t patchedSyscalls = 0;
  for (const auto& mapping : mapper.mappings()) {
    if (!mapping.executable) continue;
    for (void* site : os::SyscallPatcher::scan(mapping.hostAddress, mapping.size)) {
      if (!dispatcher.patchSite(site, error)) {
        dispatcher.uninstall();
        kernel.shutdown();
        return fail(error, "could not patch guest syscall instruction: " + error);
      }
      ++patchedSyscalls;
    }
  }
  logStep("patched " + std::to_string(patchedSyscalls) + " guest syscall sites");

  logStep("initializing Vulkan command processor");
  if (!commandProcessor.initialize(windowHandle, 1280, 720, error)) {
    dispatcher.uninstall();
    kernel.shutdown();
    return fail(error, "Vulkan initialization failed: " + error);
  }
  logStep("Vulkan command processor initialized");

  bool entryReturned = false;
  try {
    logStep("transferring control to ELF entry point 0x" + [&] {
      std::ostringstream stream;
      stream << std::hex << mapper.entryPoint();
      return stream.str();
    }());
    using EntryPoint = void (*)();
    const auto entryPoint = reinterpret_cast<EntryPoint>(
        static_cast<std::uintptr_t>(mapper.entryPoint()));
    if (entryPoint == nullptr) {
      dispatcher.uninstall();
      commandProcessor.shutdown();
      kernel.shutdown();
      return fail(error, "ELF entry point is null");
    }
    entryPoint();
    entryReturned = true;
    logStep("ELF entry point returned");
  } catch (const std::exception& exception) {
    error = "ELF entry point threw an exception: ";
    error += exception.what();
    aceps::logging::error("[BootSequence] " + error);
  } catch (...) {
    error = "ELF entry point threw an unknown exception";
    aceps::logging::error("[BootSequence] " + error);
  }

  logStep("uninstalling guest trap dispatcher");
  dispatcher.uninstall();
  commandProcessor.shutdown();
  kernel.shutdown();
  logStep("guest trap dispatcher uninstalled");

  if (!entryReturned) return false;
  error.clear();
  logStep("boot completed successfully");
  return true;
}

} // namespace aceps::core
