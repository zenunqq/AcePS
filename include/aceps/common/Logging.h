/*
 * Logging.h defines AcePS's process-wide logging adapter. Callers use the
 * stable interface while the implementation selects spdlog or a fallback.
 */
#pragma once

#include <string_view>

namespace aceps::logging {

enum class Level { Debug, Info, Warn, Error };

void initialize();
void shutdown() noexcept;
void write(Level level, std::string_view message);
void debug(std::string_view message);
void info(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);

} // namespace aceps::logging
