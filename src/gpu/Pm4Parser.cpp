/*
 * Pm4Parser.cpp validates packet headers and payload lengths without executing
 * hardware operations. Views borrow command-buffer storage for zero-copy use.
 */
#include "aceps/gpu/Pm4Parser.h"

#include <cstddef>
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

bool Pm4Parser::parseViews(std::span<const std::uint32_t> words,
                           std::vector<Pm4PacketView>& packets, std::string& error) {
  packets.clear();
  packets.reserve(words.size() / 2U + 1U);
  std::size_t index = 0;
  while (index < words.size()) {
    const std::uint32_t header = words[index++];
    const auto typeBits = header & typeMask;
    if (typeBits == type2) {
      packets.push_back(Pm4PacketView{Pm4PacketType::Type2, 0, {}});
      continue;
    }
    const auto count = static_cast<std::size_t>((header & countMask) + 1U);
    if (count > words.size() - index) {
      error = (typeBits == type0) ? "truncated type-0 PM4 packet" : "truncated type-3 PM4 packet";
      packets.clear();
      return false;
    }
    if (typeBits == type0) {
      packets.push_back(Pm4PacketView{Pm4PacketType::Type0, 0, words.subspan(index, count)});
    } else if (typeBits == type3) {
      const auto opcode = static_cast<std::uint8_t>((header & opcodeMask) >> opcodeShift);
      packets.push_back(Pm4PacketView{Pm4PacketType::Type3, opcode, words.subspan(index, count)});
    } else {
      error = "unsupported PM4 packet type";
      packets.clear();
      return false;
    }
    index += count;
  }
  error.clear();
  return true;
}

bool Pm4Parser::parse(const std::vector<std::uint32_t>& words,
                      std::vector<Pm4Packet>& packets, std::string& error) {
  std::vector<Pm4PacketView> views;
  if (!parseViews(words, views, error)) {
    packets.clear();
    return false;
  }
  packets.clear();
  packets.reserve(views.size());
  for (const auto& view : views) {
    packets.push_back(Pm4Packet{view.type, view.opcode,
                                std::vector<std::uint32_t>(view.payload.begin(), view.payload.end())});
  }
  return true;
}

} // namespace aceps::gpu
