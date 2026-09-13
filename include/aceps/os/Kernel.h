/*
 * Kernel.h defines the initial Orbis OS HLE subsystem. It owns the syscall
 * registry while borrowing the emulator's memory manager and virtual file
 * system for resource-backed handlers.
 */
#pragma once

#include "aceps/core/Subsystem.h"
#include "aceps/filesystem/VirtualFileSystem.h"
#include "aceps/memory/VirtualMemoryManager.h"
#include "aceps/os/SyscallRegistry.h"

#include <string_view>

namespace aceps::os {

class KernelSubsystem final : public core::ISubsystem {
public:
  KernelSubsystem(memory::VirtualMemoryManager& memory,
                  filesystem::VirtualFileSystem& fileSystem) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override;
  bool initialize(const core::ServiceContext& context, std::string& error) override;
  void shutdown() noexcept override;

  [[nodiscard]] SyscallRegistry& registry() noexcept;
  [[nodiscard]] const SyscallRegistry& registry() const noexcept;

private:
  memory::VirtualMemoryManager& memory_;
  filesystem::VirtualFileSystem& fileSystem_;
  SyscallRegistry registry_;
  bool initialized_{false};
};

} // namespace aceps::os
