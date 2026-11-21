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
// or 16) + request_id (4 bytes BE). Shared wire shape for both directions
// of settings sync. Slave owns the settings:
// - kMsgAudioControl (Master -> Slave): a request from Master's UI.
//   request_id is Master's id for it, increasing and nonzero.
// - kMsgAudioStatus (Slave -> Master): Slave's current settings, sent when
//   they change or after Slave applies a request. request_id is the id of
//   the last Master request Slave applied on this connection (0 if none),
//   so Master can tell which of its requests a report already reflects.
// A 6-byte payload from an older peer decodes with request_id 0.
std::vector<uint8_t> encode_audio_settings(bool send_enabled, uint32_t sample_rate, uint8_t bit_depth,
                                           uint32_t request_id);
bool decode_audio_settings(const uint8_t* data, size_t len, bool& send_enabled, uint32_t& sample_rate,
                            uint8_t& bit_depth, uint32_t& request_id);

}  // namespace nockvm::discovery
