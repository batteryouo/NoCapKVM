#include <cassert>
#include <cstdint>
#include "nockvm/discovery/monitor_protocol.h"
#include "nockvm/discovery/protocol.h"

using namespace nockvm::discovery;
using nockvm::display::MonitorInfo;

int main() {
  // Round-trip: Master
  {
    const AnnouncePacket packet = encode_announce(0x0123456789ABCDEFULL, Role::Master, 4242, "master-pc");
    DecodedAnnounce decoded;
    assert(decode_announce(packet.data(), packet.size(), decoded));
    assert(decoded.device_id == 0x0123456789ABCDEFULL);
    assert(decoded.role == Role::Master);
    assert(decoded.tcp_port == 4242);
    assert(decoded.hostname == "master-pc");
  }

  // Round-trip: Slave, zero device id and port
  {
    const AnnouncePacket packet = encode_announce(0, Role::Slave, 0, "slave-1");
    DecodedAnnounce decoded;
    assert(decode_announce(packet.data(), packet.size(), decoded));
    assert(decoded.device_id == 0);
    assert(decoded.role == Role::Slave);
    assert(decoded.tcp_port == 0);
    assert(decoded.hostname == "slave-1");
  }

  // Hostname longer than kMaxHostnameLen is truncated, not overflowed
  {
    const std::string long_name(64, 'x');
    const AnnouncePacket packet = encode_announce(1, Role::Master, 1, long_name);
    DecodedAnnounce decoded;
    assert(decode_announce(packet.data(), packet.size(), decoded));
    assert(decoded.hostname.size() == kMaxHostnameLen);
  }

  // Rejection: wrong length
  {
    const AnnouncePacket packet = encode_announce(1, Role::Master, 1, "x");
    DecodedAnnounce decoded;
    assert(!decode_announce(packet.data(), packet.size() - 1, decoded));
  }

  // Rejection: bad magic
  {
    AnnouncePacket packet = encode_announce(1, Role::Master, 1, "x");
    packet[0] ^= 0xFF;
    DecodedAnnounce decoded;
    assert(!decode_announce(packet.data(), packet.size(), decoded));
  }

  // Rejection: bad version
  {
    AnnouncePacket packet = encode_announce(1, Role::Master, 1, "x");
    packet[5] = 0xFF;
    DecodedAnnounce decoded;
    assert(!decode_announce(packet.data(), packet.size(), decoded));
  }

  // Rejection: bad role byte
  {
    AnnouncePacket packet = encode_announce(1, Role::Master, 1, "x");
    packet[14] = 0xFF;
    DecodedAnnounce decoded;
    assert(!decode_announce(packet.data(), packet.size(), decoded));
  }

  // Device id round-trip
  {
    assert(decode_device_id(encode_device_id(0)) == 0);
    assert(decode_device_id(encode_device_id(UINT64_MAX)) == UINT64_MAX);
    assert(decode_device_id(encode_device_id(0x0123456789ABCDEFULL)) == 0x0123456789ABCDEFULL);
  }

  // Monitor list round-trip: single monitor
  {
    MonitorInfo m;
    m.x = 0;
    m.y = 0;
    m.width = 1920;
    m.height = 1080;
    m.primary = true;
    m.name = "\\\\.\\DISPLAY1";

    const std::vector<uint8_t> encoded = encode_monitor_list({m});
    std::vector<MonitorInfo> decoded;
    assert(decode_monitor_list(encoded.data(), encoded.size(), decoded));
    assert(decoded.size() == 1);
    assert(decoded[0].x == m.x && decoded[0].y == m.y);
    assert(decoded[0].width == m.width && decoded[0].height == m.height);
    assert(decoded[0].primary == m.primary);
    assert(decoded[0].name == m.name);
  }

  // Monitor list round-trip: multiple monitors, including negative
  // coordinates (a monitor placed left of / above the primary).
  {
    MonitorInfo primary;
    primary.x = 0;
    primary.y = 0;
    primary.width = 1920;
    primary.height = 1080;
    primary.primary = true;
    primary.name = "eDP-1";

    MonitorInfo left;
    left.x = -1920;
    left.y = 0;
    left.width = 1920;
    left.height = 1080;
    left.primary = false;
    left.name = "HDMI-1";

    MonitorInfo above;
    above.x = 0;
    above.y = -1200;
    above.width = 2560;
    above.height = 1200;
    above.primary = false;
    above.name = "DP-2";

    const std::vector<MonitorInfo> monitors = {primary, left, above};
    const std::vector<uint8_t> encoded = encode_monitor_list(monitors);
    std::vector<MonitorInfo> decoded;
    assert(decode_monitor_list(encoded.data(), encoded.size(), decoded));
    assert(decoded.size() == 3);
    for (size_t i = 0; i < 3; ++i) {
      assert(decoded[i].x == monitors[i].x && decoded[i].y == monitors[i].y);
      assert(decoded[i].width == monitors[i].width && decoded[i].height == monitors[i].height);
      assert(decoded[i].primary == monitors[i].primary);
      assert(decoded[i].name == monitors[i].name);
    }
  }

  // Monitor list round-trip: empty list
  {
    const std::vector<uint8_t> encoded = encode_monitor_list({});
    std::vector<MonitorInfo> decoded;
    assert(decode_monitor_list(encoded.data(), encoded.size(), decoded));
    assert(decoded.empty());
  }

  // Rejection: truncated payload
  {
    MonitorInfo m;
    m.width = 100;
    m.height = 100;
    m.name = "x";
    const std::vector<uint8_t> encoded = encode_monitor_list({m});
    std::vector<MonitorInfo> decoded;
    assert(!decode_monitor_list(encoded.data(), encoded.size() - 1, decoded));
  }

  return 0;
}
