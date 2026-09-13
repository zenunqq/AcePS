/*
 * Diagnostic.h provides a lightweight success/error result for operations
 * that should report recoverable failures without using exceptions.
 */
#pragma once

#include <string>
#include <utility>
#include <variant>

namespace aceps::common {

template <typename Value>
class Result final {
public:
  static Result success(Value value) { return Result(std::move(value)); }
  static Result failure(std::string error) { return Result(std::move(error), FailureTag{}); }

  [[nodiscard]] bool hasValue() const noexcept { return std::holds_alternative<Value>(storage_); }
  [[nodiscard]] const Value& value() const { return std::get<Value>(storage_); }
  [[nodiscard]] const std::string& error() const { return std::get<std::string>(storage_); }

private:
  struct FailureTag {};
  explicit Result(Value value) : storage_(std::move(value)) {}
  Result(std::string error, FailureTag) : storage_(std::move(error)) {}
  std::variant<Value, std::string> storage_;
};

} // namespace aceps::common
