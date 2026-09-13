// AcePS virtual-file-system declaration: translates canonical guest paths into a selected host content root.
#pragma once
#include <filesystem>
#include <optional>
namespace AcePS::Content { class VirtualFileSystem { public: explicit VirtualFileSystem(std::filesystem::path root); std::optional<std::filesystem::path> resolve(const std::filesystem::path& guestPath) const; private: std::filesystem::path root_; }; }
