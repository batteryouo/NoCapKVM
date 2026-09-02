#pragma once
#include <cstdint>
#include <vector>

namespace nockvm::discovery {

// Continue the shared SecureChannel msg_type numbering after
// nockvm::input's kMsgModifierSync (= 6).
constexpr uint8_t kMsgAudioPort = 7;     // Master -> Slave: the UDP port to send audio to
constexpr uint8_t kMsgAudioStatus = 8;   // Slave -> Master: Slave's current actual audio settings
constexpr uint8_t kMsgAudioControl = 9;  // Master -> Slave: Master's requested audio settings

std::vector<uint8_t> encode_audio_port(uint16_t port);
bool decode_audio_port(const uint8_t* data, size_t len, uint16_t& port);

// send_enabled (1 byte) + sample_rate (4 bytes BE) + bit_depth (1 byte, 8
// or 16). Shared wire shape for both directions of settings sync:
// kMsgAudioStatus (Slave -> Master, sent whenever Slave's own
// send-enabled/quality settings change -- including disabling sending
// entirely, so Master's UI never gets stuck showing a stale format) and
// kMsgAudioControl (Master -> Slave, sent whenever the user changes
// settings from Master's side, as a request Slave applies to its own
// local settings).
std::vector<uint8_t> encode_audio_settings(bool send_enabled, uint32_t sample_rate, uint8_t bit_depth);
bool decode_audio_settings(const uint8_t* data, size_t len, bool& send_enabled, uint32_t& sample_rate,
                            uint8_t& bit_depth);

}  // namespace nockvm::discovery
