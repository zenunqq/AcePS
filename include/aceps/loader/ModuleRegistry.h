/*
 * ModuleRegistry.h models the import/export boundary used by ELF and SPRX
 * loading. Symbols are resolved by library and name without exposing host
 * function pointers to callers until the resolver explicitly binds them.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace aceps::loader {

using GuestAddress = std::uint64_t;

struct ExportedSymbol final {
  std::string name;
  GuestAddress address;
};

class ModuleRegistry final {
public:
  [[nodiscard]] bool registerModule(std::string moduleName, std::string& error);
  [[nodiscard]] bool registerExport(std::string_view moduleName, std::string symbolName,
                                    GuestAddress address, std::string& error);
  [[nodiscard]] bool resolve(std::string_view moduleName, std::string_view symbolName,
                             GuestAddress& address, std::string& error) const;
  [[nodiscard]] bool containsModule(std::string_view moduleName) const noexcept;
  [[nodiscard]] std::size_t moduleCount() const noexcept;

private:
  using ExportMap = std::unordered_map<std::string, GuestAddress>;
  std::unordered_map<std::string, ExportMap> modules_;
};

} // namespace aceps::loader
