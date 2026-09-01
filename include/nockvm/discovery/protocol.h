#pragma once
#include <array>
#include <cstdint>
#include "nockvm/discovery/types.h"

namespace nockvm::discovery {

struct DecodedAnnounce {
  uint64_t device_id;
  Role role;
  uint16_t tcp_port;
  std::string hostname;
};

using AnnouncePacket = std::array<uint8_t, kAnnouncePacketSize>;

AnnouncePacket encode_announce(uint64_t device_id, Role role, uint16_t tcp_port, const std::string& hostname);
bool decode_announce(const uint8_t* data, size_t len, DecodedAnnounce& out);

using DeviceIdBytes = std::array<uint8_t, 8>;

DeviceIdBytes encode_device_id(uint64_t device_id);
uint64_t decode_device_id(const DeviceIdBytes& bytes);

}  // namespace nockvm::discovery
