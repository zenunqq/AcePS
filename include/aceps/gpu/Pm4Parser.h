/*
 * Pm4Parser.h defines packet-level PM4 parsing. Packet views borrow the input
 * command buffer and avoid per-packet heap allocations on the GPU hot path.
 */
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace aceps::gpu {

enum class Pm4PacketType : std::uint8_t { Type0 = 0, Type2 = 2, Type3 = 3 };

struct Pm4PacketView final {
  Pm4PacketType type;
  std::uint8_t opcode;
  std::span<const std::uint32_t> payload;
};

struct Pm4Packet final {
  Pm4PacketType type;
  std::uint8_t opcode;
  std::vector<std::uint32_t> payload;
};

class Pm4Parser final {
public:
  [[nodiscard]] static bool parseViews(std::span<const std::uint32_t> words,
                                        std::vector<Pm4PacketView>& packets,
                                        std::string& error);
  [[nodiscard]] static bool parse(const std::vector<std::uint32_t>& words,
                                  std::vector<Pm4Packet>& packets, std::string& error);
};

} // namespace aceps::gpu
