/*
 * ApplicationConfig.h defines validated, portable user preferences. The
 * configuration object is independent from Qt and can be used headlessly.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace aceps::config {

enum class Theme : std::uint8_t { System, Light, Dark };
enum class VSyncMode : std::uint8_t { Off, On, Adaptive };

struct GraphicsConfig final {
  std::uint32_t resolutionScale{1};
  VSyncMode vsync{VSyncMode::On};
  bool fullscreen{false};
  bool anisotropicFiltering{true};
};

struct ApplicationConfig final {
  std::filesystem::path libraryDirectory{};
  std::filesystem::path saveDirectory{};
  std::filesystem::path logFile{};
  Theme theme{Theme::System};
  GraphicsConfig graphics{};
  bool debugLogging{false};

  [[nodiscard]] static ApplicationConfig defaults(const std::filesystem::path& dataDirectory);
  [[nodiscard]] bool validate(std::string& error) const;
  [[nodiscard]] bool save(const std::filesystem::path& file, std::string& error) const;
  [[nodiscard]] static std::optional<ApplicationConfig> load(const std::filesystem::path& file,
                                                               std::string& error);
};

} // namespace aceps::config
