// AcePS logger implementation: writes timestamped diagnostics to the standard error stream.
#include "support/logging/logger.h"
#include <iostream>
#include <mutex>
namespace AcePS::Support { void log(LogLevel level, std::string_view message) { static std::mutex guard; std::lock_guard lock(guard); const char* name = level == LogLevel::error ? "error" : level == LogLevel::warning ? "warning" : "info"; std::cerr << "[AcePS][" << name << "] " << message << '\n'; } }
