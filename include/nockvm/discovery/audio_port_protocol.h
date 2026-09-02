#pragma once
#include <cstdint>
#include <vector>

namespace nockvm::discovery {

// Continue the shared SecureChannel msg_type numbering after
// nockvm::input's kMsgModifierSync (= 6).
constexpr uint8_t kMsgAudioPort = 7;    // Master -> Slave: the UDP port to send audio to
constexpr uint8_t kMsgAudioFormat = 8;  // Slave -> Master: the format Slave is (re)starting capture with

std::vector<uint8_t> encode_audio_port(uint16_t port);
bool decode_audio_port(const uint8_t* data, size_t len, uint16_t& port);

// sample_rate (4 bytes BE) + bit_depth (1 byte, 8 or 16). Sent whenever
// Slave (re)starts capture -- including when the user changes the
// quality setting mid-connection -- so Master can (re)configure its
// playback device to match before more packets in the new format arrive.
std::vector<uint8_t> encode_audio_format(uint32_t sample_rate, uint8_t bit_depth);
bool decode_audio_format(const uint8_t* data, size_t len, uint32_t& sample_rate, uint8_t& bit_depth);

}  // namespace nockvm::discovery
