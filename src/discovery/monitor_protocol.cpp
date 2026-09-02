#include "nockvm/discovery/monitor_protocol.h"
#include <algorithm>
#include <cstring>

namespace nockvm::discovery {
namespace {

constexpr size_t kMaxMonitors = 32;
constexpr size_t kMaxNameLen = 32;
// x, y, width, height (int32 BE, 4 bytes each) + primary (1 byte) + name_len (1 byte).
constexpr size_t kFixedRecordLen = 4 * 4 + 1 + 1;

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

}  // namespace

std::vector<uint8_t> encode_monitor_list(const std::vector<display::MonitorInfo>& monitors) {
  std::vector<uint8_t> out;
  const uint8_t count = static_cast<uint8_t>(std::min(monitors.size(), kMaxMonitors));
  out.push_back(count);

  for (size_t i = 0; i < count; ++i) {
    const display::MonitorInfo& m = monitors[i];
    write_be32(out, m.x);
    write_be32(out, m.y);
    write_be32(out, m.width);
    write_be32(out, m.height);
    out.push_back(m.primary ? 1 : 0);
    const uint8_t name_len = static_cast<uint8_t>(std::min(m.name.size(), kMaxNameLen));
    out.push_back(name_len);
    out.insert(out.end(), m.name.begin(), m.name.begin() + name_len);
  }
  return out;
}

bool decode_monitor_list(const uint8_t* data, size_t len, std::vector<display::MonitorInfo>& out) {
  out.clear();
  if (len < 1) return false;
  const uint8_t count = data[0];
  size_t offset = 1;

  for (uint8_t i = 0; i < count; ++i) {
    if (offset + kFixedRecordLen > len) return false;
    display::MonitorInfo m;
    m.x = read_be32(data + offset);
    m.y = read_be32(data + offset + 4);
    m.width = read_be32(data + offset + 8);
    m.height = read_be32(data + offset + 12);
    m.primary = data[offset + 16] != 0;
    const uint8_t name_len = data[offset + 17];
    offset += kFixedRecordLen;
    if (offset + name_len > len) return false;
    m.name.assign(reinterpret_cast<const char*>(data + offset), name_len);
    offset += name_len;
    out.push_back(std::move(m));
  }
  return true;
}

}  // namespace nockvm::discovery
