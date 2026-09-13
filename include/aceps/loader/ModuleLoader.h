/*
 * ModuleLoader.h provides transactional ELF module loading, handle-based
 * lifetime management, symbol resolution, and safe system-library stubs.
 */
#pragma once

#include "aceps/filesystem/VirtualFileSystem.h"
#include "aceps/loader/ElfMapper.h"
#include "aceps/loader/ModuleRegistry.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace aceps::loader {

class ModuleLoader final {
public:
  ModuleLoader(memory::VirtualMemoryManager& memory,
               filesystem::VirtualFileSystem& fileSystem) noexcept;
  ~ModuleLoader();

  ModuleLoader(const ModuleLoader&) = delete;
  ModuleLoader& operator=(const ModuleLoader&) = delete;

  [[nodiscard]] bool initializeStubs(std::string& error);
  [[nodiscard]] std::uint64_t loadModule(const std::filesystem::path& path,
                                          std::string& error);
  [[nodiscard]] bool unloadModule(std::uint64_t handle, std::string& error);
  [[nodiscard]] GuestAddress resolveSymbol(std::string_view moduleName,
                                            std::string_view symbolName,
                                            std::string& error) const;
  [[nodiscard]] GuestAddress resolveSymbol(std::uint64_t handle,
                                            std::string_view symbolName,
                                            std::string& error) const;
  [[nodiscard]] std::size_t moduleCount() const noexcept;
  [[nodiscard]] std::int64_t allocateStub(std::uint64_t size, std::string& error) noexcept;
  [[nodiscard]] std::int64_t freeStub(std::uint64_t address, std::uint64_t size,
                                      std::string& error) noexcept;

private:
  struct Module final {
    std::uint64_t handle{0};
    std::string name;
    std::unique_ptr<ElfMapper> mapper;
  };

  [[nodiscard]] bool registerStubModule(std::string name,
                                         const std::vector<std::pair<std::string, GuestAddress>>& exports,
                                         std::string& error);
  [[nodiscard]] static bool readModuleImage(const std::filesystem::path& path,
                                            std::vector<std::uint8_t>& image,
                                            std::string& error);

  memory::VirtualMemoryManager& memory_;
  filesystem::VirtualFileSystem& fileSystem_;
  ModuleRegistry registry_;
  std::unordered_map<std::uint64_t, Module> modules_;
  std::unordered_map<std::uint64_t, std::string> handleNames_;
  mutable std::mutex mutex_;
  std::uint64_t nextHandle_{1};
  bool stubsInitialized_{false};
};

} // namespace aceps::loader
