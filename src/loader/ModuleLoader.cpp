/*
 * ModuleLoader.cpp implements transactional module loading and host-backed
 * stubs for the small set of system exports needed during startup.
 */
#include "aceps/loader/ModuleLoader.h"

#include "aceps/common/Logging.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

namespace aceps::loader {
namespace {

std::atomic<ModuleLoader*> activeLoader{nullptr};

std::int64_t stubGetProcessTime(const std::vector<std::uint64_t>&) { return 0; }
std::int64_t stubGetModuleInfo(const std::vector<std::uint64_t>&) { return 0; }
std::int64_t stubLoadStartModule(const std::vector<std::uint64_t>&) {
  aceps::logging::info("libkernel.sprx: sceKernelLoadStartModule requested");
  return 1;
}
std::int64_t stubDlsym(const std::vector<std::uint64_t>&) { return 0; }
std::int64_t stubPrintf(const std::vector<std::uint64_t>&) {
  aceps::logging::info("libSceLibcInternal.sprx: printf called");
  return 0;
}
std::int64_t stubMalloc(const std::vector<std::uint64_t>& arguments) {
  auto* loader = activeLoader.load(std::memory_order_acquire);
  if (loader == nullptr || arguments.empty()) return 0;
  std::string error;
  return loader->allocateStub(arguments[0], error);
}
std::int64_t stubFree(const std::vector<std::uint64_t>& arguments) {
  auto* loader = activeLoader.load(std::memory_order_acquire);
  if (loader == nullptr || arguments.size() < 2) return -1;
  std::string error;
  return loader->freeStub(arguments[0], arguments[1], error);
}
std::int64_t stubMemcpy(const std::vector<std::uint64_t>& arguments) {
  if (arguments.size() < 3) return 0;
  std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[0])),
              reinterpret_cast<const void*>(static_cast<std::uintptr_t>(arguments[1])),
              static_cast<std::size_t>(arguments[2]));
  return static_cast<std::int64_t>(arguments[0]);
}
std::int64_t stubMemset(const std::vector<std::uint64_t>& arguments) {
  if (arguments.size() < 3) return 0;
  std::memset(reinterpret_cast<void*>(static_cast<std::uintptr_t>(arguments[0])),
              static_cast<int>(arguments[1]), static_cast<std::size_t>(arguments[2]));
  return static_cast<std::int64_t>(arguments[0]);
}

GuestAddress functionAddress(std::int64_t (*function)(const std::vector<std::uint64_t>&)) {
  return reinterpret_cast<GuestAddress>(function);
}

} // namespace

ModuleLoader::ModuleLoader(memory::VirtualMemoryManager& memory,
                           filesystem::VirtualFileSystem& fileSystem) noexcept
    : memory_(memory), fileSystem_(fileSystem) {
  activeLoader.store(this, std::memory_order_release);
}

ModuleLoader::~ModuleLoader() {
  activeLoader.store(nullptr, std::memory_order_release);
  std::scoped_lock lock(mutex_);
  modules_.clear();
  handleNames_.clear();
}

std::int64_t ModuleLoader::allocateStub(std::uint64_t size, std::string& error) noexcept {
  if (size > std::numeric_limits<std::size_t>::max()) return 0;
  void* address = memory_.allocate(static_cast<std::size_t>(size), memory::Protection::ReadWrite, error);
  return address == nullptr ? 0 : static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(address));
}

std::int64_t ModuleLoader::freeStub(std::uint64_t address, std::uint64_t size,
                                    std::string& error) noexcept {
  if (size > std::numeric_limits<std::size_t>::max()) return -1;
  return memory_.release(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)),
                         static_cast<std::size_t>(size), error)
             ? 0
             : -1;
}

bool ModuleLoader::registerStubModule(
    std::string name,
    const std::vector<std::pair<std::string, GuestAddress>>& exports,
    std::string& error) {
  if (!registry_.registerModule(name, error)) return false;
  for (const auto& [symbol, address] : exports) {
    if (!registry_.registerExport(name, symbol, address, error)) {
      std::string ignored;
      (void)registry_.unregisterModule(name, ignored);
      return false;
    }
  }
  return true;
}

bool ModuleLoader::initializeStubs(std::string& error) {
  std::scoped_lock lock(mutex_);
  if (stubsInitialized_) {
    error.clear();
    return true;
  }
  if (!registerStubModule(
          "libkernel.sprx",
          {{"sceKernelGetProcessTime", functionAddress(stubGetProcessTime)},
           {"sceKernelGetModuleInfoByName", functionAddress(stubGetModuleInfo)},
           {"sceKernelLoadStartModule", functionAddress(stubLoadStartModule)},
           {"sceKernelDlsym", functionAddress(stubDlsym)}},
          error) ||
      !registerStubModule(
          "libSceLibcInternal.sprx",
          {{"printf", functionAddress(stubPrintf)},
           {"malloc", functionAddress(stubMalloc)},
           {"free", functionAddress(stubFree)},
           {"memcpy", functionAddress(stubMemcpy)},
           {"memset", functionAddress(stubMemset)}},
          error)) {
    return false;
  }
  stubsInitialized_ = true;
  error.clear();
  return true;
}

bool ModuleLoader::readModuleImage(const std::filesystem::path& path,
                                   std::vector<std::uint8_t>& image,
                                   std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "could not open module: " + path.string();
    return false;
  }
  input.seekg(0, std::ios::end);
  const auto length = input.tellg();
  if (length < 0) {
    error = "could not determine module size: " + path.string();
    return false;
  }
  input.seekg(0, std::ios::beg);
  image.resize(static_cast<std::size_t>(length));
  if (!image.empty()) input.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()));
  if (!input && !image.empty()) {
    error = "could not read module: " + path.string();
    return false;
  }
  error.clear();
  return true;
}

std::uint64_t ModuleLoader::loadModule(const std::filesystem::path& path, std::string& error) {
  std::scoped_lock lock(mutex_);
  const auto resolved = fileSystem_.resolve(path.generic_string(), error);
  if (!resolved.has_value()) return 0;

  std::vector<std::uint8_t> image;
  if (!readModuleImage(*resolved, image, error)) return 0;
  ElfLoadPlan plan{};
  if (!Elf64Loader::inspect(image, plan, error)) return 0;

  const auto moduleName = resolved->filename().string();
  if (registry_.containsModule(moduleName)) {
    error = "module is already loaded: " + moduleName;
    return 0;
  }
  auto mapper = std::make_unique<ElfMapper>();
  if (!mapper->map(image, plan, memory_, error)) return 0;
  if (!registry_.registerModule(moduleName, error)) return 0;

  const auto handle = nextHandle_++;
  handleNames_.emplace(handle, moduleName);
  modules_.emplace(handle, Module{handle, moduleName, std::move(mapper)});
  error.clear();
  aceps::logging::info("loaded module " + moduleName);
  return handle;
}

bool ModuleLoader::unloadModule(std::uint64_t handle, std::string& error) {
  std::scoped_lock lock(mutex_);
  const auto found = modules_.find(handle);
  if (found == modules_.end()) {
    error = "module handle is not loaded";
    return false;
  }
  const auto moduleName = found->second.name;
  if (!registry_.unregisterModule(moduleName, error)) return false;
  modules_.erase(found);
  handleNames_.erase(handle);
  error.clear();
  aceps::logging::info("unloaded module " + moduleName);
  return true;
}

GuestAddress ModuleLoader::resolveSymbol(std::string_view moduleName,
                                         std::string_view symbolName,
                                         std::string& error) const {
  std::scoped_lock lock(mutex_);
  GuestAddress address = 0;
  if (!registry_.resolve(moduleName, symbolName, address, error)) return 0;
  return address;
}

GuestAddress ModuleLoader::resolveSymbol(std::uint64_t handle,
                                         std::string_view symbolName,
                                         std::string& error) const {
  std::scoped_lock lock(mutex_);
  const auto found = handleNames_.find(handle);
  if (found == handleNames_.end()) {
    error = "module handle is not loaded";
    return 0;
  }
  GuestAddress address = 0;
  if (!registry_.resolve(found->second, symbolName, address, error)) return 0;
  return address;
}

std::size_t ModuleLoader::moduleCount() const noexcept {
  std::scoped_lock lock(mutex_);
  return modules_.size();
}

} // namespace aceps::loader
