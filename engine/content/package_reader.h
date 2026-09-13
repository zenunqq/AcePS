// AcePS package-reader declaration: identifies PS4 package inputs without attempting encrypted-content extraction.
#pragma once
#include <filesystem>
#include <string>
namespace AcePS::Content { struct PackageInspection { bool recognized{}; std::string detail; }; PackageInspection inspectPackage(const std::filesystem::path& path); }
