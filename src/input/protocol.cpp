#include "nockvm/input/protocol.h"

namespace nockvm::input {
namespace {

void write_be32(std::vector<uint8_t>& buf, int32_t value) {
  const auto u = static_cast<uint32_t>(value);
  buf.push_back(static_cast<uint8_t>(u >> 24));
  buf.push_back(static_cast<uint8_t>(u >> 16));
  buf.push_back(static_cast<uint8_t>(u >> 8));
  buf.push_back(static_cast<uint8_t>(u));
}

int32_t read_be32(const uint8_t* buf) {
  const uint32_t u = (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
                      (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
  return static_cast<int32_t>(u);
}

void write_be16(std::vector<uint8_t>& buf, int16_t value) {
  const auto u = static_cast<uint16_t>(value);
  buf.push_back(static_cast<uint8_t>(u >> 8));
  buf.push_back(static_cast<uint8_t>(u));
}

int16_t read_be16(const uint8_t* buf) {
  const uint16_t u = (static_cast<uint16_t>(buf[0]) << 8) | static_cast<uint16_t>(buf[1]);
  return static_cast<int16_t>(u);
}

}  // namespace

std::vector<uint8_t> encode_mouse_absolute(int32_t x, int32_t y) {
  std::vector<uint8_t> out;
  write_be32(out, x);
  write_be32(out, y);
  return out;
}

bool decode_mouse_absolute(const uint8_t* data, size_t len, int32_t& x, int32_t& y) {
  if (len < 8) return false;
  x = read_be32(data);
  y = read_be32(data + 4);
  return true;
}

std::vector<uint8_t> encode_mouse_button(uint8_t button, bool down) {
  return {button, static_cast<uint8_t>(down ? 1 : 0)};
}

bool decode_mouse_button(const uint8_t* data, size_t len, uint8_t& button, bool& down) {
  if (len < 2) return false;
  button = data[0];
  down = data[1] != 0;
  return true;
}

std::vector<uint8_t> encode_mouse_wheel(int16_t delta) {
  std::vector<uint8_t> out;
  write_be16(out, delta);
  return out;
}

bool decode_mouse_wheel(const uint8_t* data, size_t len, int16_t& delta) {
  if (len < 2) return false;
  delta = read_be16(data);
  return true;
}

std::vector<uint8_t> encode_key(uint32_t vk, uint32_t scancode, bool down, bool extended) {
  std::vector<uint8_t> out;
  write_be32(out, static_cast<int32_t>(vk));
  write_be32(out, static_cast<int32_t>(scancode));
  out.push_back(down ? 1 : 0);
  out.push_back(extended ? 1 : 0);
  return out;
}

bool decode_key(const uint8_t* data, size_t len, uint32_t& vk, uint32_t& scancode, bool& down, bool& extended) {
  if (len < 10) return false;
  vk = static_cast<uint32_t>(read_be32(data));
  scancode = static_cast<uint32_t>(read_be32(data + 4));
  down = data[8] != 0;
  extended = data[9] != 0;
  return true;
}

std::vector<uint8_t> encode_modifier_sync(uint8_t mask) { return {mask}; }

bool decode_modifier_sync(const uint8_t* data, size_t len, uint8_t& mask) {
  if (len < 1) return false;
  mask = data[0];
  return true;
}

}  // namespace nockvm::input
