/*
 * ApplicationConfig.cpp implements deterministic defaults, validation, and a
 * deliberately small key-value persistence format suitable for early builds.
 */
#include "aceps/config/ApplicationConfig.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace aceps::config {
namespace {

std::string boolString(bool value) { return value ? "true" : "false"; }

bool parseBool(std::string_view value, bool& output) {
  if (value == "true" || value == "1") {
    output = true;
    return true;
  }
  if (value == "false" || value == "0") {
    output = false;
    return true;
  }
  return false;
}

std::string themeString(Theme value) {
  switch (value) {
  case Theme::System: return "system";
  case Theme::Light: return "light";
  case Theme::Dark: return "dark";
  }
  return "system";
}

std::string vsyncString(VSyncMode value) {
  switch (value) {
  case VSyncMode::Off: return "off";
  case VSyncMode::On: return "on";
  case VSyncMode::Adaptive: return "adaptive";
  }
  return "on";
}

bool parseTheme(std::string_view value, Theme& output) {
  if (value == "system") output = Theme::System;
  else if (value == "light") output = Theme::Light;
  else if (value == "dark") output = Theme::Dark;
  else return false;
  return true;
}

bool parseVsync(std::string_view value, VSyncMode& output) {
  if (value == "off") output = VSyncMode::Off;
  else if (value == "on") output = VSyncMode::On;
  else if (value == "adaptive") output = VSyncMode::Adaptive;
  else return false;
  return true;
}

} // namespace

ApplicationConfig ApplicationConfig::defaults(const std::filesystem::path& dataDirectory) {
  ApplicationConfig config;
  config.libraryDirectory = dataDirectory / "games";
  config.saveDirectory = dataDirectory / "saves";
  config.logFile = dataDirectory / "aceps.log";
  return config;
}

bool ApplicationConfig::validate(std::string& error) const {
  if (graphics.resolutionScale == 0 || graphics.resolutionScale > 4) {
    error = "graphics.resolutionScale must be between 1 and 4";
    return false;
  }
  if (libraryDirectory.empty() || saveDirectory.empty() || logFile.empty()) {
    error = "library, save, and log paths must not be empty";
    return false;
  }
  error.clear();
  return true;
}

bool ApplicationConfig::save(const std::filesystem::path& file, std::string& error) const {
  if (!validate(error)) return false;
  std::ofstream output(file);
  if (!output) {
    error = "could not open configuration for writing: " + file.string();
    return false;
  }
  output << "library_directory=" << libraryDirectory.string() << '\n'
         << "save_directory=" << saveDirectory.string() << '\n'
         << "log_file=" << logFile.string() << '\n'
         << "theme=" << themeString(theme) << '\n'
         << "resolution_scale=" << graphics.resolutionScale << '\n'
         << "vsync=" << vsyncString(graphics.vsync) << '\n'
         << "fullscreen=" << boolString(graphics.fullscreen) << '\n'
         << "anisotropic_filtering=" << boolString(graphics.anisotropicFiltering) << '\n'
         << "debug_logging=" << boolString(debugLogging) << '\n';
  if (!output) {
    error = "failed while writing configuration: " + file.string();
    return false;
  }
  error.clear();
  return true;
}

std::optional<ApplicationConfig> ApplicationConfig::load(const std::filesystem::path& file,
                                                           std::string& error) {
  std::ifstream input(file);
  if (!input) {
    error = "could not open configuration: " + file.string();
    return std::nullopt;
  }
  ApplicationConfig config;
  std::string line;
  std::size_t lineNumber = 0;
  while (std::getline(input, line)) {
    ++lineNumber;
    if (line.empty() || line.front() == '#') continue;
    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      error = "invalid configuration line " + std::to_string(lineNumber);
      return std::nullopt;
    }
    const std::string key = line.substr(0, separator);
    const std::string value = line.substr(separator + 1);
    try {
      if (key == "library_directory") config.libraryDirectory = value;
      else if (key == "save_directory") config.saveDirectory = value;
      else if (key == "log_file") config.logFile = value;
      else if (key == "theme" && !parseTheme(value, config.theme)) throw std::runtime_error("theme");
      else if (key == "resolution_scale") config.graphics.resolutionScale = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "vsync" && !parseVsync(value, config.graphics.vsync)) throw std::runtime_error("vsync");
      else if (key == "fullscreen" && !parseBool(value, config.graphics.fullscreen)) throw std::runtime_error("fullscreen");
      else if (key == "anisotropic_filtering" && !parseBool(value, config.graphics.anisotropicFiltering)) throw std::runtime_error("anisotropic");
      else if (key == "debug_logging" && !parseBool(value, config.debugLogging)) throw std::runtime_error("debug");
      else if (key != "library_directory" && key != "save_directory" && key != "log_file" &&
               key != "theme" && key != "resolution_scale" && key != "vsync" &&
               key != "fullscreen" && key != "anisotropic_filtering" && key != "debug_logging") {
        error = "unknown configuration key '" + key + "' on line " + std::to_string(lineNumber);
        return std::nullopt;
      }
    } catch (const std::exception&) {
      error = "invalid value for '" + key + "' on line " + std::to_string(lineNumber);
      return std::nullopt;
    }
  }
  if (!config.validate(error)) return std::nullopt;
  return config;
}

} // namespace aceps::config
