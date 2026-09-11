#include "nockvm/discovery/ping_protocol.h"

namespace nockvm::discovery {

std::vector<uint8_t> encode_ping_seq(uint32_t seq) {
  return {static_cast<uint8_t>(seq >> 24), static_cast<uint8_t>(seq >> 16), static_cast<uint8_t>(seq >> 8),
          static_cast<uint8_t>(seq)};
}

bool decode_ping_seq(const uint8_t* data, size_t len, uint32_t& seq) {
  if (len < 4) return false;
  seq = (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
        (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
  return true;
}

}  // namespace nockvm::discovery
