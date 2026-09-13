/*
 * BootSequence.h defines the ordered startup path for a raw ELF64 workload.
 * It owns all boot-scoped services and guarantees syscall handler cleanup.
 */
#pragma once

#include <filesystem>
#include <string>

namespace aceps::core {

class BootSequence final {
public:
  [[nodiscard]] bool run(const std::filesystem::path& elfPath, std::string& error) const;
};

} // namespace aceps::core
