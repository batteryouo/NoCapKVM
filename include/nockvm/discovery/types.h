#pragma once
#include <chrono>
#include <cstdint>
#include <string>

namespace nockvm::discovery {

enum class Role : uint8_t { Master = 0, Slave = 1 };

constexpr uint32_t kProtocolMagic = 0x4E4B564D;  // "NKVM"
constexpr uint16_t kProtocolVersion = 1;
constexpr uint16_t kDiscoveryPort = 47821;
constexpr size_t kAnnouncePacketSize = 50;
constexpr size_t kMaxHostnameLen = 32;
constexpr auto kPeerTimeout = std::chrono::seconds(5);

struct PeerInfo {
  uint64_t device_id;
  std::string hostname;
  Role role;
  uint16_t tcp_port;
  std::string ip_address;
  std::chrono::steady_clock::time_point last_seen;
};

}  // namespace nockvm::discovery
