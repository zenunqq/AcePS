/*
 * ModuleRegistry.cpp implements deterministic module/import resolution. A
 * missing symbol is reported explicitly instead of being bound to a stub by
 * accident.
 */
#include "aceps/loader/ModuleRegistry.h"

namespace aceps::loader {

bool ModuleRegistry::registerModule(std::string moduleName, std::string& error) {
  if (moduleName.empty()) {
    error = "module name must not be empty";
    return false;
  }
  if (!modules_.emplace(std::move(moduleName), ExportMap{}).second) {
    error = "module is already registered";
    return false;
  }
  error.clear();
  return true;
}

bool ModuleRegistry::registerExport(std::string_view moduleName, std::string symbolName,
                                    GuestAddress address, std::string& error) {
  const auto module = modules_.find(std::string(moduleName));
  if (module == modules_.end()) {
    error = "module is not registered";
    return false;
  }
  if (symbolName.empty() || address == 0) {
    error = "export name and address must be valid";
    return false;
  }
  if (!module->second.emplace(std::move(symbolName), address).second) {
    error = "export is already registered";
    return false;
  }
  error.clear();
  return true;
}

bool ModuleRegistry::resolve(std::string_view moduleName, std::string_view symbolName,
                             GuestAddress& address, std::string& error) const {
  const auto module = modules_.find(std::string(moduleName));
  if (module == modules_.end()) {
    error = "module is not registered";
    return false;
  }
  const auto symbol = module->second.find(std::string(symbolName));
  if (symbol == module->second.end()) {
    error = "export is not registered";
    return false;
  }
  address = symbol->second;
  error.clear();
  return true;
}

bool ModuleRegistry::containsModule(std::string_view moduleName) const noexcept {
  return modules_.contains(std::string(moduleName));
}

std::size_t ModuleRegistry::moduleCount() const noexcept { return modules_.size(); }

} // namespace aceps::loader
