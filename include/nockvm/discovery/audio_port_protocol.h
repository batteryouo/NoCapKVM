#pragma once
#include <cstdint>
#include <vector>

namespace nockvm::discovery {

// Continues the shared SecureChannel msg_type numbering after
// nockvm::input's kMsgModifierSync (= 6).
constexpr uint8_t kMsgAudioPort = 7;

std::vector<uint8_t> encode_audio_port(uint16_t port);
bool decode_audio_port(const uint8_t* data, size_t len, uint16_t& port);

}  // namespace nockvm::discovery
