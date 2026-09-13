// AcePS package-reader implementation: validates package availability and leaves decryption as an explicit future step.
#include "engine/content/package_reader.h"
#include <filesystem>
namespace AcePS::Content { PackageInspection inspectPackage(const std::filesystem::path&p){if(!std::filesystem::is_regular_file(p))return{false,"Content path is not a readable package file."};if(p.extension()!=".pkg")return{false,"Only .pkg package files are currently recognized."};return{true,"Package recognized; PFS decryption and mounting are not implemented yet."};} }
