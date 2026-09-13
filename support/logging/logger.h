// AcePS logger declaration: provides thread-safe, user-readable diagnostic output for host services.
#pragma once
#include <string_view>
namespace AcePS::Support { enum class LogLevel { info, warning, error }; void log(LogLevel level, std::string_view message); }
