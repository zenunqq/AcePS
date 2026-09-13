/*
 * Emulator.cpp coordinates the service lifecycle. Startup is transactional:
 * if a service fails, already-started services are rolled back in reverse
 * order and the object enters Failed rather than exposing partial readiness.
 */
#include "aceps/core/Emulator.h"

#include "aceps/common/Logging.h"
#include "aceps/core/EmulatorError.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace aceps::core {
namespace {

class LifecycleMarker final : public ISubsystem {
public:
  explicit LifecycleMarker(std::string_view subsystemName) : name_(subsystemName) {}

  [[nodiscard]] std::string_view name() const noexcept override { return name_; }

  bool initialize(const ServiceContext&, std::string& error) override {
    error.clear();
    initialized_ = true;
    return true;
  }

  void shutdown() noexcept override { initialized_ = false; }

private:
  std::string name_;
  bool initialized_{false};
};

config::ApplicationConfig normalizeConfig(config::ApplicationConfig config) {
  if (config.libraryDirectory.empty() && config.saveDirectory.empty() && config.logFile.empty()) {
    config = config::ApplicationConfig::defaults(std::filesystem::current_path() / ".aceps");
  }
  return config;
}

} // namespace

Emulator::Emulator(config::ApplicationConfig config) : config_(normalizeConfig(std::move(config))) {
  subsystems_.reserve(5);
  subsystems_.push_back(std::make_unique<LifecycleMarker>("memory"));
  subsystems_.push_back(std::make_unique<LifecycleMarker>("kernel"));
  subsystems_.push_back(std::make_unique<LifecycleMarker>("loader"));
  subsystems_.push_back(std::make_unique<LifecycleMarker>("graphics"));
  subsystems_.push_back(std::make_unique<LifecycleMarker>("input"));
}

Emulator::~Emulator() {
  shutdown();
}

void Emulator::initialize() {
  if (state_ == EmulatorState::Running) {
    return;
  }
  if (state_ == EmulatorState::Failed) {
    throw EmulatorError("cannot initialize an emulator that previously failed");
  }

  std::string validationError;
  if (!config_.validate(validationError)) {
    state_ = EmulatorState::Failed;
    throw EmulatorError("invalid configuration: " + validationError);
  }

  const ServiceContext context{version()};
  for (auto& subsystem : subsystems_) {
    std::string error;
    aceps::logging::info(std::string("Starting subsystem: ") + std::string(subsystem->name()));
    if (!subsystem->initialize(context, error)) {
      for (std::size_t index = initializedSubsystemCount_; index > 0; --index) {
        subsystems_[index - 1]->shutdown();
      }
      initializedSubsystemCount_ = 0;
      state_ = EmulatorState::Failed;
      throw EmulatorError("failed to initialize " + std::string(subsystem->name()) + ": " + error);
    }
    ++initializedSubsystemCount_;
  }
  state_ = EmulatorState::Running;
  aceps::logging::info("AcePS services initialized");
}

void Emulator::shutdown() noexcept {
  if (initializedSubsystemCount_ == 0 && state_ != EmulatorState::Running) {
    return;
  }
  for (std::size_t index = initializedSubsystemCount_; index > 0; --index) {
    subsystems_[index - 1]->shutdown();
  }
  initializedSubsystemCount_ = 0;
  state_ = EmulatorState::Stopped;
}

EmulatorState Emulator::state() const noexcept { return state_; }

bool Emulator::isInitialized() const noexcept { return state_ == EmulatorState::Running; }

std::string_view Emulator::version() const noexcept { return ACEPS_VERSION; }

const config::ApplicationConfig& Emulator::configuration() const noexcept { return config_; }

std::size_t Emulator::initializedSubsystemCount() const noexcept { return initializedSubsystemCount_; }

} // namespace aceps::core
