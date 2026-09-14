/* ScePad.cpp registers libScePad HLE exports with the fixed PS4 syscall ABI. */
#include "aceps/input/ScePad.h"
#include "aceps/input/InputSystem.h"
#include "aceps/os/SyscallRegistry.h"

#include <cstring>
#include <utility>
#include <vector>

namespace aceps::input {
namespace {
constexpr std::uint32_t kInit = 580U;
constexpr std::uint32_t kOpen = 581U;
constexpr std::uint32_t kClose = 582U;
constexpr std::uint32_t kReadState = 583U;
constexpr std::uint32_t kRead = 584U;
constexpr std::uint32_t kSetMotionSensorState = 585U;
constexpr std::uint32_t kGetControllerInformation = 586U;
constexpr std::uint32_t kSetLightBar = 587U;
constexpr std::uint32_t kResetLightBar = 588U;
constexpr std::uint32_t kSetVibration = 589U;

struct SceControllerInformation final {
  std::uint8_t touchpadPixelDensity{44};
  std::uint8_t stickDeadZoneLeft{13};
  std::uint8_t stickDeadZoneRight{13};
  std::uint8_t connectionType{1};
  std::uint8_t connectedCount{0};
  std::uint8_t _pad[3]{};
};

bool handleToSlot(const std::vector<std::uint64_t>& args, int& slot) noexcept {
  if (args.empty() || args[0] < 1U || args[0] > 4U) return false;
  slot = static_cast<int>(args[0] - 1U);
  return true;
}

os::SyscallResult readOne(InputSystem& input, const std::vector<std::uint64_t>& args) {
  int slot = 0;
  if (!handleToSlot(args, slot) || args.size() < 2 || args[1] == 0) return SCE_PAD_ERROR_INVALID_ARG;
  ScePadData state{};
  std::string localError;
  if (!input.readState(slot, state, localError)) return SCE_PAD_ERROR_INVALID_HANDLE;
  std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(args[1])), &state, sizeof(state));
  return 0;
}

} // namespace

bool ScePad::registerHandlers(InputSystem& input, os::SyscallRegistry& registry,
                              std::string& error) {
  const auto add = [&registry, &error](const std::uint32_t number, os::SyscallHandler handler) {
    return registry.registerHandler(number, std::move(handler), error);
  };
  if (!add(kInit, [&input](const auto&) {
        std::string localError;
        return static_cast<os::SyscallResult>(input.initialize(localError) ? 0 : SCE_PAD_ERROR_NO_DEVICE);
      }) ||
      !add(kOpen, [&input](const auto& args) {
        if (args.size() < 3 || args[2] > 3U) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_ARG);
        const auto slot = static_cast<int>(args[2]);
        std::string localError;
        if (!input.openSlot(slot, localError)) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_ALREADY_OPENED);
        return static_cast<os::SyscallResult>(slot + 1);
      }) ||
      !add(kClose, [&input](const auto& args) {
        int slot = 0;
        if (!handleToSlot(args, slot)) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_HANDLE);
        std::string localError;
        return static_cast<os::SyscallResult>(input.closeSlot(slot, localError) ? 0 : SCE_PAD_ERROR_NOT_OPENED);
      }) ||
      !add(kReadState, [&input](const auto& args) { return readOne(input, args); }) ||
      !add(kRead, [&input](const auto& args) {
        if (args.size() < 3 || args[2] == 0) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_ARG);
        const auto result = readOne(input, {args[0], args[1]});
        return result == 0 ? static_cast<os::SyscallResult>(1) : result;
      }) ||
      !add(kSetMotionSensorState, [&input](const auto& args) {
        int slot = 0;
        if (!handleToSlot(args, slot) || args.size() < 2) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_ARG);
        (void)input;
        return static_cast<os::SyscallResult>(0);
      }) ||
      !add(kGetControllerInformation, [&input](const auto& args) {
        int slot = 0;
        if (!handleToSlot(args, slot) || args.size() < 2 || args[1] == 0) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_ARG);
        ScePadData state{};
        std::string localError;
        if (!input.readState(slot, state, localError)) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_HANDLE);
        SceControllerInformation info{};
        info.connectedCount = state.connectedCount;
        std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(args[1])), &info, sizeof(info));
        return static_cast<os::SyscallResult>(0);
      }) ||
      !add(kSetLightBar, [&input](const auto& args) {
        int slot = 0;
        if (!handleToSlot(args, slot) || args.size() < 2 || args[1] == 0) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_ARG);
        const auto* color = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(args[1]));
        input.setLed(slot, color[0], color[1], color[2]);
        return static_cast<os::SyscallResult>(0);
      }) ||
      !add(kResetLightBar, [&input](const auto& args) {
        int slot = 0;
        if (!handleToSlot(args, slot)) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_HANDLE);
        input.setLed(slot, 0, 0, 255);
        return static_cast<os::SyscallResult>(0);
      }) ||
      !add(kSetVibration, [&input](const auto& args) {
        int slot = 0;
        if (!handleToSlot(args, slot) || args.size() < 2 || args[1] == 0) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_ARG);
        const auto* vibration = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(args[1]));
        input.setRumble(slot, vibration[0], vibration[1]);
        return static_cast<os::SyscallResult>(0);
      })) return false;
  error.clear();
  return true;
}

} // namespace aceps::input
