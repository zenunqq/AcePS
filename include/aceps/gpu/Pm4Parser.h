/*
 * Pm4Parser.h defines the first packet-level PM4 parser. It validates packet
 * bounds and separates packet type/opcode from payload for later execution.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aceps::gpu {

enum class Pm4PacketType : std::uint8_t { Type0 = 0, Type2 = 2, Type3 = 3 };

struct Pm4Packet final {
  Pm4PacketType type;
  std::uint8_t opcode;
  std::vector<std::uint32_t> payload;
};

class Pm4Parser final {
public:
  [[nodiscard]] static bool parse(const std::vector<std::uint32_t>& words,
                                  std::vector<Pm4Packet>& packets, std::string& error);
};

} // namespace aceps::gpu
