/*
 * Emulator.h exposes the top-level AcePS lifecycle and service composition.
 * It owns startup ordering, rollback on partial failure, and deterministic
 * shutdown while keeping subsystem implementations behind ISubsystem.
 */
#pragma once

#include "aceps/config/ApplicationConfig.h"
#include "aceps/core/Subsystem.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace aceps::core {

enum class EmulatorState : unsigned char { Created, Running, Stopped, Failed };

class Emulator final {
public:
  explicit Emulator(config::ApplicationConfig config = {});
  ~Emulator();

  Emulator(const Emulator&) = delete;
  Emulator& operator=(const Emulator&) = delete;
  Emulator(Emulator&&) = delete;
  Emulator& operator=(Emulator&&) = delete;

  void initialize();
  void shutdown() noexcept;
  [[nodiscard]] EmulatorState state() const noexcept;
  [[nodiscard]] bool isInitialized() const noexcept;
  [[nodiscard]] std::string_view version() const noexcept;
  [[nodiscard]] const config::ApplicationConfig& configuration() const noexcept;
  [[nodiscard]] std::size_t initializedSubsystemCount() const noexcept;

private:
  config::ApplicationConfig config_;
  std::vector<std::unique_ptr<ISubsystem>> subsystems_;
  EmulatorState state_{EmulatorState::Created};
  std::size_t initializedSubsystemCount_{0};
};

} // namespace aceps::core
