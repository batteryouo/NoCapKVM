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

std::vector<uint8_t> encode_audio_settings(bool send_enabled, uint32_t sample_rate, uint8_t bit_depth) {
  return {static_cast<uint8_t>(send_enabled ? 1 : 0),
          static_cast<uint8_t>(sample_rate >> 24),
          static_cast<uint8_t>(sample_rate >> 16),
          static_cast<uint8_t>(sample_rate >> 8),
          static_cast<uint8_t>(sample_rate),
          bit_depth};
}

bool decode_audio_settings(const uint8_t* data, size_t len, bool& send_enabled, uint32_t& sample_rate,
                            uint8_t& bit_depth) {
  if (len < 6) return false;
  send_enabled = data[0] != 0;
  sample_rate = (static_cast<uint32_t>(data[1]) << 24) | (static_cast<uint32_t>(data[2]) << 16) |
                (static_cast<uint32_t>(data[3]) << 8) | static_cast<uint32_t>(data[4]);
  bit_depth = data[5];
  return true;
}

}  // namespace nockvm::discovery
