/*
 * Subsystem.h defines the small lifecycle contract shared by emulator
 * services. Dependencies are passed explicitly through the context to keep
 * startup order observable and shutdown deterministic.
 */
#pragma once

#include <string_view>

namespace aceps::core {

struct ServiceContext final {
  std::string_view version;
};

class ISubsystem {
public:
  virtual ~ISubsystem() = default;
  ISubsystem(const ISubsystem&) = delete;
  ISubsystem& operator=(const ISubsystem&) = delete;

  [[nodiscard]] virtual std::string_view name() const noexcept = 0;
  virtual bool initialize(const ServiceContext& context, std::string& error) = 0;
  virtual void shutdown() noexcept = 0;

protected:
  ISubsystem() = default;
};

} // namespace aceps::core
