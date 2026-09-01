#include <cassert>
#include <cstdint>
#include "nockvm/discovery/protocol.h"

using namespace nockvm::discovery;

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

  return 0;
}
