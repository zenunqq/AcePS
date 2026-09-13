/*
 * Pm4Benchmark.cpp measures the allocation-free PM4 view parser locally. It
 * is intentionally opt-in and reports throughput for regression comparisons.
 */
#include "aceps/gpu/Pm4Parser.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
  constexpr std::size_t packetCount = 100000;
  std::vector<std::uint32_t> commandBuffer;
  commandBuffer.reserve(packetCount * 2U);
  for (std::size_t index = 0; index < packetCount; ++index) {
    commandBuffer.push_back(0xC0C00000U);
    commandBuffer.push_back(static_cast<std::uint32_t>(index));
  }

  std::vector<aceps::gpu::Pm4PacketView> packets;
  std::string error;
  const auto start = std::chrono::steady_clock::now();
  const bool valid = aceps::gpu::Pm4Parser::parseViews(commandBuffer, packets, error);
  const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (!valid) {
    std::cerr << "PM4 benchmark input rejected: " << error << '\n';
    return 1;
  }
  const double packetsPerSecond = static_cast<double>(packets.size()) / elapsed;
  std::cout << packets.size() << " packets in " << elapsed << " s ("
            << packetsPerSecond << " packets/s)\n";
  return 0;
}
