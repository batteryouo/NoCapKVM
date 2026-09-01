#include "nockvm/discovery/protocol.h"
#include <algorithm>
#include <cstring>

namespace nockvm::discovery {
namespace {

constexpr size_t kOffMagic = 0;
constexpr size_t kOffVersion = 4;
constexpr size_t kOffDeviceId = 6;
constexpr size_t kOffRole = 14;
constexpr size_t kOffTcpPort = 15;
constexpr size_t kOffHostnameLen = 17;
constexpr size_t kOffHostname = 18;

void write_be32(uint8_t* buf, uint32_t value) {
  buf[0] = static_cast<uint8_t>(value >> 24);
  buf[1] = static_cast<uint8_t>(value >> 16);
  buf[2] = static_cast<uint8_t>(value >> 8);
  buf[3] = static_cast<uint8_t>(value);
}

void write_be16(uint8_t* buf, uint16_t value) {
  buf[0] = static_cast<uint8_t>(value >> 8);
  buf[1] = static_cast<uint8_t>(value);
}

uint32_t read_be32(const uint8_t* buf) {
  return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
         (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
}

uint16_t read_be16(const uint8_t* buf) {
  return static_cast<uint16_t>((static_cast<uint16_t>(buf[0]) << 8) | static_cast<uint16_t>(buf[1]));
}

}  // namespace

AnnouncePacket encode_announce(uint64_t device_id, Role role, uint16_t tcp_port, const std::string& hostname) {
  AnnouncePacket packet{};
  uint8_t* buf = packet.data();
  write_be32(buf + kOffMagic, kProtocolMagic);
  write_be16(buf + kOffVersion, kProtocolVersion);
  write_be32(buf + kOffDeviceId, static_cast<uint32_t>(device_id >> 32));
  write_be32(buf + kOffDeviceId + 4, static_cast<uint32_t>(device_id));
  buf[kOffRole] = static_cast<uint8_t>(role);
  write_be16(buf + kOffTcpPort, tcp_port);
  const uint8_t hostname_len = static_cast<uint8_t>(std::min(hostname.size(), kMaxHostnameLen));
  buf[kOffHostnameLen] = hostname_len;
  std::memcpy(buf + kOffHostname, hostname.data(), hostname_len);
  return packet;
}

bool decode_announce(const uint8_t* data, size_t len, DecodedAnnounce& out) {
  if (len != kAnnouncePacketSize) return false;
  if (read_be32(data + kOffMagic) != kProtocolMagic) return false;
  if (read_be16(data + kOffVersion) != kProtocolVersion) return false;
  const uint8_t role_byte = data[kOffRole];
  if (role_byte != static_cast<uint8_t>(Role::Master) && role_byte != static_cast<uint8_t>(Role::Slave)) return false;
  const uint8_t hostname_len = data[kOffHostnameLen];
  if (hostname_len > kMaxHostnameLen) return false;
  const uint64_t high = read_be32(data + kOffDeviceId);
  const uint64_t low = read_be32(data + kOffDeviceId + 4);
  out.device_id = (high << 32) | low;
  out.role = static_cast<Role>(role_byte);
  out.tcp_port = read_be16(data + kOffTcpPort);
  out.hostname.assign(reinterpret_cast<const char*>(data + kOffHostname), hostname_len);
  return true;
}

}  // namespace nockvm::discovery
