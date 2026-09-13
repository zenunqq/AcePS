/*
 * Pm4Parser.cpp validates packet headers and payload lengths without executing
 * hardware operations. Malformed input is rejected with an actionable error.
 */
#include "aceps/gpu/Pm4Parser.h"

#include <limits>
#include <utility>

namespace aceps::gpu {
namespace {
constexpr std::uint32_t typeMask = 0xC0000000U;
constexpr std::uint32_t type0 = 0x00000000U;
constexpr std::uint32_t type2 = 0x80000000U;
constexpr std::uint32_t type3 = 0xC0000000U;
constexpr std::uint32_t countMask = 0x00003FFFU;
constexpr std::uint32_t opcodeMask = 0x00FF0000U;
constexpr std::uint32_t opcodeShift = 16U;
} // namespace

bool Pm4Parser::parse(const std::vector<std::uint32_t>& words,
                      std::vector<Pm4Packet>& packets, std::string& error) {
  packets.clear();
  std::size_t index = 0;
  while (index < words.size()) {
    const std::uint32_t header = words[index++];
    const auto typeBits = header & typeMask;
    if (typeBits == type2) {
      packets.push_back(Pm4Packet{Pm4PacketType::Type2, 0, {}});
      continue;
    }
    if (typeBits == type0) {
      const auto count = static_cast<std::size_t>((header & countMask) + 1U);
      if (index + count > words.size()) {
        error = "truncated type-0 PM4 packet";
        packets.clear();
        return false;
      }
      std::vector<std::uint32_t> payload(words.begin() + static_cast<std::ptrdiff_t>(index),
                                         words.begin() + static_cast<std::ptrdiff_t>(index + count));
      packets.push_back(Pm4Packet{Pm4PacketType::Type0, 0, std::move(payload)});
      index += count;
      continue;
    }
    if (typeBits == type3) {
      const auto count = static_cast<std::size_t>((header & countMask) + 1U);
      if (index + count > words.size()) {
        error = "truncated type-3 PM4 packet";
        packets.clear();
        return false;
      }
      const auto opcode = static_cast<std::uint8_t>((header & opcodeMask) >> opcodeShift);
      std::vector<std::uint32_t> payload(words.begin() + static_cast<std::ptrdiff_t>(index),
                                         words.begin() + static_cast<std::ptrdiff_t>(index + count));
      packets.push_back(Pm4Packet{Pm4PacketType::Type3, opcode, std::move(payload)});
      index += count;
      continue;
    }
    error = "unsupported PM4 packet type";
    packets.clear();
    return false;
  }
  error.clear();
  return true;
}

} // namespace aceps::gpu
