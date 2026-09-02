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
  // Master-side only: the format Slave last reported via kMsgAudioFormat
  // (0 sample_rate means "not reported yet, or Slave isn't sending audio").
  uint32_t peer_audio_sample_rate = 0;
  uint8_t peer_audio_bit_depth = 16;
};

}  // namespace nockvm::discovery
