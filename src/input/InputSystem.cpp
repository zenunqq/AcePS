/*
 * InputSystem.cpp implements SDL3 gamepad discovery, PS4-style mapping, and
 * a keyboard fallback for slot zero when no physical pad is connected.
 */
#include "aceps/input/InputSystem.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#if defined(ACEPS_HAS_SDL3)
#include <SDL3/SDL.h>
#endif

namespace aceps::input {
namespace {
constexpr std::uint8_t kStickCenter = 128U;

#if defined(ACEPS_HAS_SDL3)
std::uint8_t axisToByte(const std::int16_t value) noexcept {
  const auto shifted = static_cast<std::int32_t>(value) + 32768;
  return static_cast<std::uint8_t>(std::clamp(shifted / 256, 0, 255));
}

std::uint8_t triggerToByte(const std::int16_t value) noexcept {
  return static_cast<std::uint8_t>(std::clamp((static_cast<std::int32_t>(value) * 255) / 32767, 0, 255));
}
#endif

} // namespace

InputSystem::InputSystem() = default;
InputSystem::~InputSystem() { shutdown(); }

std::uint64_t InputSystem::nowMicroseconds() noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

bool InputSystem::initialize(std::string& error) {
  std::scoped_lock lock(mutex_);
  if (initialized_) {
    error.clear();
    return true;
  }
#if defined(ACEPS_HAS_SDL3)
  if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_SENSOR)) {
    error = SDL_GetError();
    return false;
  }
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  if (ids != nullptr) {
    for (int index = 0; index < count; ++index) handleDeviceAdded(ids[index]);
    SDL_free(ids);
  }
#endif
  initialized_ = true;
  error.clear();
  return true;
}

void InputSystem::handleDeviceAdded(const std::int32_t sdlJoystickId) {
#if defined(ACEPS_HAS_SDL3)
  for (const auto& slot : slots_) {
    if (slot.sdlGamepad != nullptr && SDL_GetGamepadID(static_cast<SDL_Gamepad*>(slot.sdlGamepad)) == sdlJoystickId) return;
  }
  auto found = std::find_if(slots_.begin(), slots_.end(), [](const Slot& slot) { return !slot.connected; });
  if (found == slots_.end()) return;
  SDL_Gamepad* gamepad = SDL_OpenGamepad(static_cast<SDL_JoystickID>(sdlJoystickId));
  if (gamepad == nullptr) return;
  found->sdlGamepad = gamepad;
  found->connected = true;
  found->connectedCount = 1;
  found->timestampBase = nowMicroseconds();
  SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_ACCEL, true);
  SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_GYRO, true);
#else
  (void)sdlJoystickId;
#endif
}

void InputSystem::handleDeviceRemoved(const std::int32_t sdlInstanceId) {
#if defined(ACEPS_HAS_SDL3)
  for (auto& slot : slots_) {
    if (slot.sdlGamepad == nullptr || SDL_GetGamepadID(static_cast<SDL_Gamepad*>(slot.sdlGamepad)) != sdlInstanceId) continue;
    SDL_CloseGamepad(static_cast<SDL_Gamepad*>(slot.sdlGamepad));
    slot.sdlGamepad = nullptr;
    slot.connected = false;
    slot.connectedCount = 0;
  }
#else
  (void)sdlInstanceId;
#endif
}

void InputSystem::pump() {
  std::scoped_lock lock(mutex_);
  if (!initialized_) return;
#if defined(ACEPS_HAS_SDL3)
  SDL_PumpEvents();
  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) handleDeviceAdded(event.gdevice.which);
    else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) handleDeviceRemoved(event.gdevice.which);
  }
#endif
}

void InputSystem::fillState(const Slot& slot, const int slotIndex, ScePadData& out) const {
  out = ScePadData{};
  out.leftStickX = kStickCenter;
  out.leftStickY = kStickCenter;
  out.rightStickX = kStickCenter;
  out.rightStickY = kStickCenter;
  out.connected = slot.connected ? 1U : 0U;
  out.connectedCount = slot.connectedCount;
  out.timestamp = nowMicroseconds() >= slot.timestampBase ? nowMicroseconds() - slot.timestampBase : 0;
#if defined(ACEPS_HAS_SDL3)
  if (slot.sdlGamepad != nullptr) {
    auto* gamepad = static_cast<SDL_Gamepad*>(slot.sdlGamepad);
    const auto button = [gamepad](const SDL_GamepadButton value) { return SDL_GetGamepadButton(gamepad, value); };
    if (button(SDL_GAMEPAD_BUTTON_SOUTH)) out.buttons |= ScePadButton_Cross;
    if (button(SDL_GAMEPAD_BUTTON_EAST)) out.buttons |= ScePadButton_Circle;
    if (button(SDL_GAMEPAD_BUTTON_WEST)) out.buttons |= ScePadButton_Square;
    if (button(SDL_GAMEPAD_BUTTON_NORTH)) out.buttons |= ScePadButton_Triangle;
    if (button(SDL_GAMEPAD_BUTTON_LEFT_BUMPER)) out.buttons |= ScePadButton_L1;
    if (button(SDL_GAMEPAD_BUTTON_RIGHT_BUMPER)) out.buttons |= ScePadButton_R1;
    if (button(SDL_GAMEPAD_BUTTON_BACK)) out.buttons |= ScePadButton_TouchPad;
    if (button(SDL_GAMEPAD_BUTTON_START)) out.buttons |= ScePadButton_Options;
    if (button(SDL_GAMEPAD_BUTTON_LEFT_STICK)) out.buttons |= ScePadButton_L3;
    if (button(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) out.buttons |= ScePadButton_R3;
    if (button(SDL_GAMEPAD_BUTTON_DPAD_UP)) out.buttons |= ScePadButton_Up;
    if (button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) out.buttons |= ScePadButton_Right;
    if (button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) out.buttons |= ScePadButton_Down;
    if (button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) out.buttons |= ScePadButton_Left;
    out.leftStickX = axisToByte(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX));
    out.leftStickY = axisToByte(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));
    out.rightStickX = axisToByte(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX));
    out.rightStickY = axisToByte(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));
    out.analogL2 = triggerToByte(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    out.analogR2 = triggerToByte(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    if (out.analogL2 > 128U) out.buttons |= ScePadButton_L2;
    if (out.analogR2 > 128U) out.buttons |= ScePadButton_R2;
    float sensor[3]{};
    if (SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_ACCEL, sensor, 3)) {
      out.accelX = sensor[0]; out.accelY = sensor[1]; out.accelZ = sensor[2];
    }
    if (SDL_GetGamepadSensorData(gamepad, SDL_SENSOR_GYRO, sensor, 3)) {
      out.gyroX = sensor[0]; out.gyroY = sensor[1]; out.gyroZ = sensor[2];
    }
    return;
  }
  if (slotIndex == 0) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    const auto pressed = [&keys](const SDL_Scancode key) { return keys != nullptr && keys[key]; };
    out.connected = 1U;
    out.connectedCount = 1U;
    out.leftStickX = static_cast<std::uint8_t>(pressed(SDL_SCANCODE_A) ? 64U : pressed(SDL_SCANCODE_D) ? 192U : 128U);
    out.leftStickY = static_cast<std::uint8_t>(pressed(SDL_SCANCODE_W) ? 64U : pressed(SDL_SCANCODE_S) ? 192U : 128U);
    out.rightStickX = static_cast<std::uint8_t>(pressed(SDL_SCANCODE_LEFT) ? 64U : pressed(SDL_SCANCODE_RIGHT) ? 192U : 128U);
    out.rightStickY = static_cast<std::uint8_t>(pressed(SDL_SCANCODE_UP) ? 64U : pressed(SDL_SCANCODE_DOWN) ? 192U : 128U);
    if (pressed(SDL_SCANCODE_RETURN)) out.buttons |= ScePadButton_Options;
    if (pressed(SDL_SCANCODE_SPACE)) out.buttons |= ScePadButton_Cross;
    if (pressed(SDL_SCANCODE_Z)) out.buttons |= ScePadButton_Square;
    if (pressed(SDL_SCANCODE_X)) out.buttons |= ScePadButton_Circle;
    if (pressed(SDL_SCANCODE_C)) out.buttons |= ScePadButton_Triangle;
    if (pressed(SDL_SCANCODE_Q)) out.buttons |= ScePadButton_L1;
    if (pressed(SDL_SCANCODE_E)) out.buttons |= ScePadButton_R1;
    if (pressed(SDL_SCANCODE_1)) { out.analogL2 = 255U; out.buttons |= ScePadButton_L2; }
    if (pressed(SDL_SCANCODE_3)) { out.analogR2 = 255U; out.buttons |= ScePadButton_R2; }
    if (pressed(SDL_SCANCODE_F)) out.buttons |= ScePadButton_TouchPad;
  }
#else
  (void)slotIndex;
#endif
}

bool InputSystem::readState(const int slot, ScePadData& out, std::string& error) {
  std::scoped_lock lock(mutex_);
  if (slot < 0 || slot >= kMaxSlots) { error = "input slot is out of range"; return false; }
  fillState(slots_[static_cast<std::size_t>(slot)], slot, out);
  error.clear();
  return true;
}

bool InputSystem::openSlot(const int slot, std::string& error) {
  std::scoped_lock lock(mutex_);
  if (slot < 0 || slot >= kMaxSlots) { error = "input slot is out of range"; return false; }
  auto& value = slots_[static_cast<std::size_t>(slot)];
  if (value.open) { error = "input slot is already open"; return false; }
  value.open = true;
  error.clear();
  return true;
}

bool InputSystem::closeSlot(const int slot, std::string& error) {
  std::scoped_lock lock(mutex_);
  if (slot < 0 || slot >= kMaxSlots) { error = "input slot is out of range"; return false; }
  auto& value = slots_[static_cast<std::size_t>(slot)];
  if (!value.open) { error = "input slot is not open"; return false; }
  value.open = false;
  error.clear();
  return true;
}

bool InputSystem::isSlotOpen(const int slot) const noexcept {
  std::scoped_lock lock(mutex_);
  return slot >= 0 && slot < kMaxSlots && slots_[static_cast<std::size_t>(slot)].open;
}

void InputSystem::setRumble(const int slot, const std::uint8_t large, const std::uint8_t small) {
  std::scoped_lock lock(mutex_);
#if defined(ACEPS_HAS_SDL3)
  if (slot >= 0 && slot < kMaxSlots && slots_[static_cast<std::size_t>(slot)].sdlGamepad != nullptr) {
    SDL_RumbleGamepad(static_cast<SDL_Gamepad*>(slots_[static_cast<std::size_t>(slot)].sdlGamepad),
                      static_cast<std::uint16_t>(large) * 257U, static_cast<std::uint16_t>(small) * 257U, 200U);
  }
#else
  (void)slot; (void)large; (void)small;
#endif
}

void InputSystem::setLed(const int slot, const std::uint8_t r, const std::uint8_t g, const std::uint8_t b) {
  std::scoped_lock lock(mutex_);
#if defined(ACEPS_HAS_SDL3)
  if (slot >= 0 && slot < kMaxSlots && slots_[static_cast<std::size_t>(slot)].sdlGamepad != nullptr) {
    SDL_SetGamepadLED(static_cast<SDL_Gamepad*>(slots_[static_cast<std::size_t>(slot)].sdlGamepad), r, g, b);
  }
#else
  (void)slot; (void)r; (void)g; (void)b;
#endif
}

int InputSystem::connectedCount() const noexcept {
  std::scoped_lock lock(mutex_);
  int count = 0;
  for (const auto& slot : slots_) if (slot.connected) ++count;
  return count;
}

void InputSystem::shutdown() noexcept {
  std::scoped_lock lock(mutex_);
#if defined(ACEPS_HAS_SDL3)
  for (auto& slot : slots_) {
    if (slot.sdlGamepad != nullptr) SDL_CloseGamepad(static_cast<SDL_Gamepad*>(slot.sdlGamepad));
    slot.sdlGamepad = nullptr;
  }
  if (initialized_) SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_SENSOR);
#endif
  slots_ = {};
  initialized_ = false;
}

} // namespace aceps::input
