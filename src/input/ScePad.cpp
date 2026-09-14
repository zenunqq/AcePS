/* ScePad.cpp registers the initial libScePad HLE exports. */
#include "aceps/input/ScePad.h"
#include "aceps/input/InputSystem.h"
#include "aceps/os/SyscallRegistry.h"

#include <cerrno>
#include <cstring>
#include <utility>

namespace aceps::input {
namespace {
// These IDs are kept in one place so the ABI table can be replaced by the
// title-specific resolver when module export metadata becomes available.
constexpr std::uint32_t kOpen = 700U;
constexpr std::uint32_t kClose = 701U;
constexpr std::uint32_t kRead = 702U;
constexpr std::uint32_t kSetVibration = 703U;
constexpr std::uint32_t kSetLightBar = 704U;
constexpr std::uint32_t kGetConnectionStatus = 705U;
constexpr std::uint32_t kIsConnected = 706U;

bool decodeHandle(const std::vector<std::uint64_t>& args, int& slot) noexcept {
  if (args.empty() || args[0] < 1U || args[0] > 4U) return false;
  slot = static_cast<int>(args[0] - 1U);
  return true;
}

os::SyscallResult copyState(InputSystem& input, const std::vector<std::uint64_t>& args) {
  int slot = 0;
  if (!decodeHandle(args, slot) || args.size() < 2 || args[1] == 0) return SCE_PAD_ERROR_INVALID_ARG;
  ScePadData state{};
  std::string error;
  if (!input.readState(slot, state, error)) return SCE_PAD_ERROR_INVALID_HANDLE;
  std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(args[1])), &state, sizeof(state));
  return 0;
}

} // namespace

bool ScePad::registerHandlers(InputSystem& input, os::SyscallRegistry& registry,
                              std::string& error) {
  const auto add = [&registry, &error](const std::uint32_t number, os::SyscallHandler handler) {
    return registry.registerHandler(number, std::move(handler), error);
  };
  if (!add(kOpen, [&input](const auto& args) {
        if (args.empty() || args[0] > 3U) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_ARG);
        const auto slot = static_cast<int>(args[0]);
        std::string localError;
        if (!input.openSlot(slot, localError)) {
          return static_cast<os::SyscallResult>(input.isSlotOpen(slot) ? SCE_PAD_ERROR_ALREADY_OPENED
                                                                        : SCE_PAD_ERROR_INVALID_ARG);
        }
        return static_cast<os::SyscallResult>(slot + 1);
      }) ||
      !add(kClose, [&input](const auto& args) {
        int slot = 0;
        if (!decodeHandle(args, slot)) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_HANDLE);
        std::string localError;
        return input.closeSlot(slot, localError) ? static_cast<os::SyscallResult>(0)
                                                 : static_cast<os::SyscallResult>(SCE_PAD_ERROR_NOT_OPENED);
      }) ||
      !add(kRead, [&input](const auto& args) { return copyState(input, args); }) ||
      !add(kSetVibration, [&input](const auto& args) {
        int slot = 0;
        if (!decodeHandle(args, slot) || args.size() < 3) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_ARG);
        input.setRumble(slot, static_cast<std::uint8_t>(args[1]), static_cast<std::uint8_t>(args[2]));
        return static_cast<os::SyscallResult>(0);
      }) ||
      !add(kSetLightBar, [&input](const auto& args) {
        int slot = 0;
        if (!decodeHandle(args, slot) || args.size() < 4) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_ARG);
        input.setLed(slot, static_cast<std::uint8_t>(args[1]), static_cast<std::uint8_t>(args[2]), static_cast<std::uint8_t>(args[3]));
        return static_cast<os::SyscallResult>(0);
      }) ||
      !add(kGetConnectionStatus, [&input](const auto& args) {
        if (args.empty() || args[0] == 0) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_ARG);
        int slot = 0;
        if (!decodeHandle(args, slot)) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_HANDLE);
        if (args.size() < 2 || args[1] == 0) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_ARG);
        ScePadData state{};
        std::string localError;
        if (!input.readState(slot, state, localError)) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_HANDLE);
        std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(args[1])), &state.connected, sizeof(state.connected));
        return static_cast<os::SyscallResult>(0);
      }) ||
      !add(kIsConnected, [&input](const auto& args) {
        int slot = 0;
        if (!decodeHandle(args, slot)) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_HANDLE);
        ScePadData state{};
        std::string localError;
        if (!input.readState(slot, state, localError)) return static_cast<os::SyscallResult>(SCE_PAD_ERROR_INVALID_HANDLE);
        return static_cast<os::SyscallResult>(state.connected != 0U ? 1 : 0);
      })) return false;
  error.clear();
  return true;
}

} // namespace aceps::input
