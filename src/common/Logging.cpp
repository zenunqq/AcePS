/*
 * Logging.cpp implements a thread-safe process logger. spdlog is preferred;
 * the fallback remains useful for dependency-light CI and never uses stdout.
 */
#include "aceps/common/Logging.h"

#include <iostream>
#include <mutex>
#include <utility>

#ifdef ACEPS_NO_SPDLOG
#else
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#endif

namespace aceps::logging {
namespace {
std::mutex loggerMutex;

#ifndef ACEPS_NO_SPDLOG
void ensureLogger() {
  if (!spdlog::default_logger()) {
    auto logger = spdlog::stdout_color_mt("aceps");
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_pattern("[%H:%M:%S] [%^%l%$] %v");
  }
}
#endif
} // namespace

void initialize() {
  std::scoped_lock lock(loggerMutex);
#ifndef ACEPS_NO_SPDLOG
  ensureLogger();
#endif
}

void shutdown() noexcept {
  std::scoped_lock lock(loggerMutex);
#ifndef ACEPS_NO_SPDLOG
  spdlog::shutdown();
#endif
}

void write(Level level, std::string_view message) {
  std::scoped_lock lock(loggerMutex);
#ifdef ACEPS_NO_SPDLOG
  switch (level) {
  case Level::Info:
    std::clog << "[INFO] ";
    break;
  case Level::Warn:
    std::clog << "[WARN] ";
    break;
  case Level::Error:
    std::clog << "[ERROR] ";
    break;
  }
  std::clog << message << '\n';
#else
  ensureLogger();
  switch (level) {
  case Level::Info:
    spdlog::info("{}", message);
    break;
  case Level::Warn:
    spdlog::warn("{}", message);
    break;
  case Level::Error:
    spdlog::error("{}", message);
    break;
  }
#endif
}

void info(std::string_view message) { write(Level::Info, message); }
void warn(std::string_view message) { write(Level::Warn, message); }
void error(std::string_view message) { write(Level::Error, message); }

} // namespace aceps::logging
