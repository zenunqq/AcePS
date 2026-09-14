/*
 * InputSystem.h provides four pad slots with an optional SDL3 backend and a
 * deterministic keyboard fallback for slot zero.
 */
#pragma once

#include "aceps/input/ScePad.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

namespace aceps::input {

class InputSystem final {
public:
  InputSystem();
  ~InputSystem();
  InputSystem(const InputSystem&) = delete;
  InputSystem& operator=(const InputSystem&) = delete;
  InputSystem(InputSystem&&) = delete;
  InputSystem& operator=(InputSystem&&) = delete;

  [[nodiscard]] bool initialize(std::string& error);
  void pump();
  [[nodiscard]] bool readState(int slot, ScePadData& out, std::string& error);
  [[nodiscard]] bool openSlot(int slot, std::string& error);
  [[nodiscard]] bool closeSlot(int slot, std::string& error);
  [[nodiscard]] bool isSlotOpen(int slot) const noexcept;
  void setRumble(int slot, std::uint8_t large, std::uint8_t small);
  void setLed(int slot, std::uint8_t r, std::uint8_t g, std::uint8_t b);
  [[nodiscard]] int connectedCount() const noexcept;
  void shutdown() noexcept;

private:
  static constexpr int kMaxSlots = 4;
  struct Slot final {
    bool open{false};
    bool connected{false};
    void* sdlGamepad{nullptr};
    std::uint8_t connectedCount{0};
    std::uint64_t timestampBase{0};
  };

  std::array<Slot, kMaxSlots> slots_{};
  bool initialized_{false};
  mutable std::mutex mutex_;

  void handleDeviceAdded(std::int32_t sdlJoystickId);
  void handleDeviceRemoved(std::int32_t sdlInstanceId);
  void fillState(const Slot& slot, int slotIndex, ScePadData& out) const;
  static std::uint64_t nowMicroseconds() noexcept;
};

} // namespace aceps::input
