#include "nockvm/discovery/audio_port_protocol.h"

namespace nockvm::discovery {

std::vector<uint8_t> encode_audio_port(uint16_t port) {
  return {static_cast<uint8_t>(port >> 8), static_cast<uint8_t>(port)};
}

bool decode_audio_port(const uint8_t* data, size_t len, uint16_t& port) {
  if (len < 2) return false;
  port = static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
  return true;
}

}  // namespace nockvm::discovery
