// AcePS graphics-device declaration: defines a Vulkan-first presentation contract while GPU translation is developed.
#pragma once
#include <string>
namespace AcePS::Graphics { enum class Backend { vulkan, unavailable }; class GraphicsDevice { public: bool initialize(Backend backend, std::string& reason); unsigned resolutionScale()const{return resolutionScale_;} void setResolutionScale(unsigned scale){resolutionScale_=scale?scale:1;} private: unsigned resolutionScale_=1; }; }
