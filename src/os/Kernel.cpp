/*
 * Kernel.cpp implements the initial file, memory, process, thread, and
 * synchronization HLE calls needed by a small legal ELF test workload.
 */
#include "aceps/os/Kernel.h"

#include "aceps/common/Logging.h"
#include "aceps/audio/SceAudioOut.h"
#include "aceps/loader/PkgLoader.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <pthread.h>
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
constexpr SyscallNumber kCreateThread = 557;
constexpr SyscallNumber kStartThread = 558;
constexpr SyscallNumber kExitThread = 559;
constexpr SyscallNumber kDeleteThread = 560;
constexpr SyscallNumber kGetThreadId = 561;
constexpr SyscallNumber kCreateMutex = 564;
constexpr SyscallNumber kLockMutex = 565;
constexpr SyscallNumber kUnlockMutex = 566;
constexpr SyscallNumber kDeleteMutex = 567;
constexpr SyscallNumber kCreateSema = 568;
constexpr SyscallNumber kWaitSema = 569;
constexpr SyscallNumber kSignalSema = 570;
constexpr SyscallNumber kDeleteSema = 571;
constexpr SyscallNumber kPrintf = 572;
constexpr SyscallNumber kUsleep = 573;
constexpr SyscallNumber kSleep = 574;
constexpr SyscallNumber kDlsym = 591;
constexpr SyscallNumber kLoadStartModule = 594;
constexpr SyscallNumber kStopUnloadModule = 595;
constexpr SyscallNumber kInstallHandler = 596;
constexpr SyscallNumber kGnmSubmitCommandBuffers = 1000;
constexpr SyscallNumber kIsNeoMode = 615;
constexpr std::size_t kMaxPrintfLength = 4096;

thread_local std::uint64_t currentGuestThreadId = 1;

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

bool toCount(std::uint64_t value, int& result) noexcept {
  if (value == 0 || value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return false;
  result = static_cast<int>(value);
  return true;
}

bool toInitialCount(std::uint64_t value, int& result) noexcept {
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
                                 filesystem::VirtualFileSystem& fileSystem,
                                 gpu::CommandProcessor* commandProcessor) noexcept
    : memory_(memory), fileSystem_(fileSystem), commandProcessor_(commandProcessor),
      moduleLoader_(memory, fileSystem) {}

KernelSubsystem::~KernelSubsystem() { shutdown(); }

std::string_view KernelSubsystem::name() const noexcept { return "kernel"; }

bool KernelSubsystem::initialize(const core::ServiceContext&, std::string& error) {
  if (initialized_) {
    error.clear();
    return true;
  }
  if (registry_.size() == 37) {
    initialized_ = true;
    error.clear();
    return true;
  }

  const auto registerHandler = [this, &error](SyscallNumber number, SyscallHandler handler) {
    return registry_.registerHandler(number, std::move(handler), error);
  };
  if (!moduleLoader_.initializeStubs(error)) return false;
  if (!audio_.initialize(error) || !audio::SceAudioOut::registerHandlers(audio_, registry_, error)) return false;
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
      !registerHandler(kIsNeoMode, [](const auto&) { return static_cast<SyscallResult>(0); }) ||
      !registerHandler(kCreateThread, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 2) || arguments[1] == 0) return static_cast<SyscallResult>(-EINVAL);
        std::scoped_lock lock(threadTableMutex_);
        const auto id = nextThreadId_++;
        threads_.emplace(id, ThreadEntry{std::thread{}, arguments[1],
                                          argumentAvailable(arguments, 3) ? arguments[2] : 0, false});
        return static_cast<SyscallResult>(id);
      }) ||
      !registerHandler(kStartThread, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        std::scoped_lock lock(threadTableMutex_);
        const auto found = threads_.find(arguments[0]);
        if (found == threads_.end()) return static_cast<SyscallResult>(-ESRCH);
        if (found->second.started) return static_cast<SyscallResult>(-EALREADY);
        auto& entry = found->second;
        const auto id = found->first;
        entry.started = true;
        try {
          entry.worker = std::thread([this, id, entryPoint = entry.entryPoint, argument = entry.argument] {
            currentGuestThreadId = id;
            using ThreadEntryPoint = void (*)(std::uint64_t);
            auto function = reinterpret_cast<ThreadEntryPoint>(static_cast<std::uintptr_t>(entryPoint));
            if (function != nullptr) function(argument);
            currentGuestThreadId = 1;
          });
        } catch (...) {
          entry.started = false;
          return static_cast<SyscallResult>(-EAGAIN);
        }
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kExitThread, [](const auto&) {
#if defined(_WIN32)
        ::ExitThread(0);
#else
        ::pthread_exit(nullptr);
#endif
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kDeleteThread, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        std::thread worker;
        {
          std::scoped_lock lock(threadTableMutex_);
          const auto found = threads_.find(arguments[0]);
          if (found == threads_.end()) return static_cast<SyscallResult>(-ESRCH);
          if (found->first == currentGuestThreadId) return static_cast<SyscallResult>(-EDEADLK);
          worker = std::move(found->second.worker);
          threads_.erase(found);
        }
        if (worker.joinable()) worker.join();
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kGetThreadId, [](const auto&) { return static_cast<SyscallResult>(currentGuestThreadId); }) ||
      !registerHandler(kCreateMutex, [this](const auto&) {
        std::scoped_lock lock(mutexTableMutex_);
        const auto handle = nextMutexHandle_++;
        mutexes_.emplace(handle, std::make_shared<std::mutex>());
        return static_cast<SyscallResult>(handle);
      }) ||
      !registerHandler(kLockMutex, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        std::shared_ptr<std::mutex> mutex;
        {
          std::scoped_lock lock(mutexTableMutex_);
          const auto found = mutexes_.find(arguments[0]);
          if (found == mutexes_.end()) return static_cast<SyscallResult>(-EINVAL);
          mutex = found->second;
        }
        mutex->lock();
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kUnlockMutex, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        std::shared_ptr<std::mutex> mutex;
        {
          std::scoped_lock lock(mutexTableMutex_);
          const auto found = mutexes_.find(arguments[0]);
          if (found == mutexes_.end()) return static_cast<SyscallResult>(-EINVAL);
          mutex = found->second;
        }
        mutex->unlock();
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kDeleteMutex, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        std::scoped_lock lock(mutexTableMutex_);
        const auto found = mutexes_.find(arguments[0]);
        if (found == mutexes_.end()) return static_cast<SyscallResult>(-EINVAL);
        if (!found->second->try_lock()) return static_cast<SyscallResult>(-EBUSY);
        found->second->unlock();
        mutexes_.erase(found);
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kCreateSema, [this](const auto& arguments) {
        int count = 1;
        if (argumentAvailable(arguments, 1) && !toInitialCount(arguments[0], count)) return static_cast<SyscallResult>(-EINVAL);
        std::scoped_lock lock(semaphoreTableMutex_);
        const auto handle = nextSemaphoreHandle_++;
        semaphores_.emplace(handle, std::make_shared<Semaphore>(count));
        return static_cast<SyscallResult>(handle);
      }) ||
      !registerHandler(kWaitSema, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        int count = 1;
        if (argumentAvailable(arguments, 2) && !toCount(arguments[1], count)) return static_cast<SyscallResult>(-EINVAL);
        std::shared_ptr<Semaphore> semaphore;
        {
          std::scoped_lock lock(semaphoreTableMutex_);
          const auto found = semaphores_.find(arguments[0]);
          if (found == semaphores_.end()) return static_cast<SyscallResult>(-EINVAL);
          semaphore = found->second;
        }
        for (int index = 0; index < count; ++index) semaphore->acquire();
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kSignalSema, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        int count = 1;
        if (argumentAvailable(arguments, 2) && !toCount(arguments[1], count)) return static_cast<SyscallResult>(-EINVAL);
        std::shared_ptr<Semaphore> semaphore;
        {
          std::scoped_lock lock(semaphoreTableMutex_);
          const auto found = semaphores_.find(arguments[0]);
          if (found == semaphores_.end()) return static_cast<SyscallResult>(-EINVAL);
          semaphore = found->second;
        }
        try {
          semaphore->release(count);
        } catch (...) {
          return static_cast<SyscallResult>(-EOVERFLOW);
        }
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kDeleteSema, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        std::scoped_lock lock(semaphoreTableMutex_);
        const auto found = semaphores_.find(arguments[0]);
        if (found == semaphores_.end()) return static_cast<SyscallResult>(-EINVAL);
        semaphores_.erase(found);
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kUsleep, [](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        std::this_thread::sleep_for(std::chrono::microseconds(arguments[0]));
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kSleep, [](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        std::this_thread::sleep_for(std::chrono::seconds(arguments[0]));
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kLoadStartModule, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        std::string path;
        if (!guestString(arguments[0], path)) return static_cast<SyscallResult>(-EFAULT);
        std::string error;
        const auto handle = moduleLoader_.loadModule(path, error);
        return handle == 0 ? static_cast<SyscallResult>(-ENOENT)
                           : static_cast<SyscallResult>(handle);
      }) ||
      !registerHandler(kStopUnloadModule, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        std::string error;
        return moduleLoader_.unloadModule(arguments[0], error) ? static_cast<SyscallResult>(0)
                                                               : static_cast<SyscallResult>(-EINVAL);
      }) ||
      !registerHandler(kDlsym, [this](const auto& arguments) {
        if (!argumentAvailable(arguments, 2)) return static_cast<SyscallResult>(-EINVAL);
        std::string symbol;
        if (!guestString(arguments[1], symbol)) return static_cast<SyscallResult>(-EFAULT);
        std::string error;
        const auto address = moduleLoader_.resolveSymbol(arguments[0], symbol, error);
        return address == 0 ? static_cast<SyscallResult>(-ENOENT)
                            : static_cast<SyscallResult>(address);
      }) ||
      !registerHandler(kInstallHandler, [](const auto& arguments) {
        if (!argumentAvailable(arguments, 1)) return static_cast<SyscallResult>(-EINVAL);
        std::string path;
        if (!guestString(arguments[0], path)) return static_cast<SyscallResult>(-EFAULT);
        loader::PkgLoader package;
        std::string error;
        if (!package.parse(path, error)) {
          aceps::logging::error(error);
          return static_cast<SyscallResult>(-EINVAL);
        }
        aceps::logging::info("PKG content ID: " + package.contentId());
        if (!package.info().title.empty()) aceps::logging::info("PKG title: " + package.info().title);
        return static_cast<SyscallResult>(0);
      }) ||
      !registerHandler(kGnmSubmitCommandBuffers, [this](const auto& arguments) {
        if (commandProcessor_ == nullptr || arguments.size() < 2) return static_cast<SyscallResult>(-ENODEV);
        if (arguments[1] > std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t)) {
          return static_cast<SyscallResult>(-EINVAL);
        }
        const auto* words = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(arguments[0]));
        const auto count = static_cast<std::size_t>(arguments[1]);
        if (words == nullptr || count == 0) return static_cast<SyscallResult>(-EINVAL);
        std::string error;
        if (!commandProcessor_->submit(std::span<const std::uint32_t>(words, count), error) ||
            !commandProcessor_->endFrame(error)) {
          aceps::logging::error("sceGnmSubmitCommandBuffers failed: " + error);
          return static_cast<SyscallResult>(-EIO);
        }
        return static_cast<SyscallResult>(0);
      })) {
    return false;
  }

  initialized_ = true;
  error.clear();
  return true;
}

void KernelSubsystem::shutdown() noexcept {
  audio_.shutdown();
  std::vector<std::thread> workers;
  {
    std::scoped_lock lock(threadTableMutex_);
    for (auto& [id, entry] : threads_) {
      (void)id;
      if (entry.worker.joinable()) workers.push_back(std::move(entry.worker));
    }
    threads_.clear();
  }
  for (auto& worker : workers) {
    if (!worker.joinable()) continue;
    if (worker.get_id() == std::this_thread::get_id()) {
      worker.detach();
    } else {
      worker.join();
    }
  }
  {
    std::scoped_lock lock(mutexTableMutex_);
    mutexes_.clear();
  }
  {
    std::scoped_lock lock(semaphoreTableMutex_);
    semaphores_.clear();
  }
  initialized_ = false;
}

SyscallRegistry& KernelSubsystem::registry() noexcept { return registry_; }

const SyscallRegistry& KernelSubsystem::registry() const noexcept { return registry_; }

} // namespace aceps::os
