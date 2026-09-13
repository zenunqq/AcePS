// AcePS input-router declaration: represents a normalized controller state for SDL/HID adapters to populate later.
#pragma once
#include <cstdint>
namespace AcePS::Controls { struct ControllerState { std::uint32_t buttons{}; float leftX{},leftY{},rightX{},rightY{}; }; class InputRouter { public: const ControllerState& controller(unsigned index)const; private: ControllerState controllers_[4]{}; }; }
