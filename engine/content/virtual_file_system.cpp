// AcePS virtual-file-system implementation: rejects path traversal before constructing host paths.
#include "engine/content/virtual_file_system.h"
namespace AcePS::Content { VirtualFileSystem::VirtualFileSystem(std::filesystem::path r):root_(std::move(r)){} std::optional<std::filesystem::path> VirtualFileSystem::resolve(const std::filesystem::path&p)const{if(p.is_absolute())return {};for(const auto&part:p)if(part=="..")return {};return root_/p;} }
