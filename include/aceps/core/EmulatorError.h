/*
 * EmulatorError.h provides a typed exception for failures that prevent safe
 * startup or execution, allowing the UI and future CLI to present context.
 */
#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace aceps::core {

class EmulatorError final : public std::runtime_error {
public:
  explicit EmulatorError(std::string message) : std::runtime_error(std::move(message)) {}
};

} // namespace aceps::core
