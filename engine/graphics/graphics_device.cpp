// AcePS graphics-device implementation: safely reports that GCN command and shader processing remains unavailable.
#include "engine/graphics/graphics_device.h"
namespace AcePS::Graphics { bool GraphicsDevice::initialize(Backend b,std::string&r){if(b!=Backend::vulkan){r="No graphics fallback is available.";return false;}r="Vulkan GCN translation has not been implemented yet.";return false;} }
