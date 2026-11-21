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

namespace {

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

uint32_t get_u32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

}  // namespace

std::vector<uint8_t> encode_audio_settings(bool send_enabled, uint32_t sample_rate, uint8_t bit_depth,
                                           uint32_t request_id) {
  std::vector<uint8_t> out;
  out.reserve(10);
  out.push_back(static_cast<uint8_t>(send_enabled ? 1 : 0));
  put_u32(out, sample_rate);
  out.push_back(bit_depth);
  put_u32(out, request_id);
  return out;
}

bool decode_audio_settings(const uint8_t* data, size_t len, bool& send_enabled, uint32_t& sample_rate,
                            uint8_t& bit_depth, uint32_t& request_id) {
  if (len < 6) return false;
  send_enabled = data[0] != 0;
  sample_rate = get_u32(data + 1);
  bit_depth = data[5];
  // Older peers send only the first 6 bytes.
  request_id = len >= 10 ? get_u32(data + 6) : 0;
  return true;
}

}  // namespace nockvm::discovery
