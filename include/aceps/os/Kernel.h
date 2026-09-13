/*
 * Kernel.h defines the initial Orbis OS HLE subsystem. It owns the syscall
 * registry and guest thread/synchronization handles while borrowing the
 * emulator's memory manager and virtual file system.
 */
#pragma once

#include "aceps/core/Subsystem.h"
#include "aceps/filesystem/VirtualFileSystem.h"
#include "aceps/memory/VirtualMemoryManager.h"
#include "aceps/os/SyscallRegistry.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <semaphore>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace aceps::os {

class KernelSubsystem final : public core::ISubsystem {
public:
  KernelSubsystem(memory::VirtualMemoryManager& memory,
                  filesystem::VirtualFileSystem& fileSystem) noexcept;
  ~KernelSubsystem() override;

  [[nodiscard]] std::string_view name() const noexcept override;
  bool initialize(const core::ServiceContext& context, std::string& error) override;
  void shutdown() noexcept override;

  [[nodiscard]] SyscallRegistry& registry() noexcept;
  [[nodiscard]] const SyscallRegistry& registry() const noexcept;

private:
  using Semaphore = std::counting_semaphore<>;
  struct ThreadEntry final {
    std::thread worker;
    std::uint64_t entryPoint{0};
    std::uint64_t argument{0};
    bool started{false};
  };

  memory::VirtualMemoryManager& memory_;
  filesystem::VirtualFileSystem& fileSystem_;
  SyscallRegistry registry_;
  std::unordered_map<std::uint64_t, ThreadEntry> threads_;
  std::unordered_map<std::uint64_t, std::shared_ptr<std::mutex>> mutexes_;
  std::unordered_map<std::uint64_t, std::shared_ptr<Semaphore>> semaphores_;
  std::mutex threadTableMutex_;
  std::mutex mutexTableMutex_;
  std::mutex semaphoreTableMutex_;
  std::uint64_t nextThreadId_{2};
  std::uint64_t nextMutexHandle_{1};
  std::uint64_t nextSemaphoreHandle_{1};
  bool initialized_{false};
};

} // namespace aceps::os
