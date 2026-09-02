#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "nockvm/discovery/noise_primitives.h"
#include "nockvm/display/monitor_info.h"

namespace nockvm::discovery {

enum class ConnectionState : uint8_t { Idle, Connecting, Pairing, Connected, Failed };

struct ConnectionInfo {
  ConnectionState state = ConnectionState::Idle;
  uint64_t peer_device_id = 0;
  std::string peer_ip;
  std::string pairing_fingerprint;              // set only while state == Pairing
  std::vector<display::MonitorInfo> peer_monitors;  // populated once the peer reports its displays
  // The Noise transport key for the Slave-to-Master data direction
  // specifically (not "this machine's own send/recv key", which differ by
  // role) -- TcpServer sets this to ik.keys.recv_key, TcpClient to
  // ik.keys.send_key, so both sides end up holding the identical value and
  // can independently derive the same audio::derive_audio_key() from it.
  Key32 audio_key{};
  uint16_t peer_audio_port = 0;  // Slave-side only: the UDP port Master reported via kMsgAudioPort
};

}  // namespace nockvm::discovery
