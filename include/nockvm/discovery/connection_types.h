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

  // Master-side only: Slave's current actual audio settings, last reported
  // via kMsgAudioStatus. peer_audio_send_enabled defaults to false, which
  // doubles as "unknown" (nothing reported yet this connection) and
  // "explicitly disabled" -- both mean "don't expect audio right now".
  bool peer_audio_send_enabled = false;
  uint32_t peer_audio_sample_rate = 0;
  uint8_t peer_audio_bit_depth = 16;

  // Slave-side only: Master's latest requested audio settings, reported
  // via kMsgAudioControl. master_audio_control_seq increments on every
  // received control message so the app layer can apply a new request
  // exactly once (edge-triggered) instead of re-applying it every frame,
  // which would otherwise fight a local edit made in between.
  bool master_audio_control_received = false;
  bool master_requested_send_enabled = true;
  uint32_t master_requested_sample_rate = 48000;
  uint8_t master_requested_bit_depth = 16;
  uint32_t master_audio_control_seq = 0;

  // Slave-side only: set when kMsgGoAway is seen on this connection attempt
  // (Master closing on purpose, not the link just dying). The app layer
  // (auto_connect_pump.cpp) is responsible for turning this into a
  // per-device suppression -- TcpClient itself doesn't track anything
  // beyond a single attempt's lifetime.
  bool go_away_received = false;
};

}  // namespace nockvm::discovery
