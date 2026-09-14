/*
 * ScePad.h exposes the guest-visible libScePad ABI and HLE registration.
 */
#pragma once

#include <cstdint>
#include <string>

namespace aceps::os { class SyscallRegistry; }
namespace aceps::input {

constexpr std::uint32_t ScePadButton_TouchPad = 0x0001U;
constexpr std::uint32_t ScePadButton_L3 = 0x0002U;
constexpr std::uint32_t ScePadButton_R3 = 0x0004U;
constexpr std::uint32_t ScePadButton_Options = 0x0008U;
constexpr std::uint32_t ScePadButton_Up = 0x0010U;
constexpr std::uint32_t ScePadButton_Right = 0x0020U;
constexpr std::uint32_t ScePadButton_Down = 0x0040U;
constexpr std::uint32_t ScePadButton_Left = 0x0080U;
constexpr std::uint32_t ScePadButton_L2 = 0x0100U;
constexpr std::uint32_t ScePadButton_R2 = 0x0200U;
constexpr std::uint32_t ScePadButton_L1 = 0x0400U;
constexpr std::uint32_t ScePadButton_R1 = 0x0800U;
constexpr std::uint32_t ScePadButton_Triangle = 0x1000U;
constexpr std::uint32_t ScePadButton_Circle = 0x2000U;
constexpr std::uint32_t ScePadButton_Cross = 0x4000U;
constexpr std::uint32_t ScePadButton_Square = 0x8000U;

constexpr std::int32_t SCE_PAD_ERROR_INVALID_ARG = static_cast<std::int32_t>(0x80920001U);
constexpr std::int32_t SCE_PAD_ERROR_ALREADY_OPENED = static_cast<std::int32_t>(0x80920002U);
constexpr std::int32_t SCE_PAD_ERROR_NOT_OPENED = static_cast<std::int32_t>(0x80920003U);
constexpr std::int32_t SCE_PAD_ERROR_INVALID_HANDLE = static_cast<std::int32_t>(0x80920005U);
constexpr std::int32_t SCE_PAD_ERROR_NO_DEVICE = static_cast<std::int32_t>(0x8092000FU);
constexpr std::int32_t SCE_PAD_ERROR_DEVICE_NOT_FOUND = static_cast<std::int32_t>(0x80920010U);

struct ScePadTouch final {
  std::uint8_t id{0};
  std::uint16_t x{0};
  std::uint16_t y{0};
};

struct ScePadData final {
  std::uint32_t buttons{0};
  std::uint8_t leftStickX{128};
  std::uint8_t leftStickY{128};
  std::uint8_t rightStickX{128};
  std::uint8_t rightStickY{128};
  std::uint8_t analogL2{0};
  std::uint8_t analogR2{0};
  std::uint8_t _pad[2]{};
  std::uint8_t touchCount{0};
  ScePadTouch touch[2]{};
  std::uint64_t timestamp{0};
  float accelX{0.0F};
  float accelY{0.0F};
  float accelZ{0.0F};
  float gyroX{0.0F};
  float gyroY{0.0F};
  float gyroZ{0.0F};
  std::uint8_t connected{0};
  std::uint8_t connectedCount{0};
  std::uint8_t _pad2[2]{};
};

class InputSystem;
class ScePad final {
public:
  static bool registerHandlers(InputSystem& input, os::SyscallRegistry& registry,
                               std::string& error);
};

} // namespace aceps::input
