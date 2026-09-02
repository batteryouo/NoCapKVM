#include "nockvm/audio/protocol.h"
#include <sodium.h>

namespace nockvm::audio {
namespace {

void write_be32(std::vector<uint8_t>& buf, uint32_t value) {
  buf.push_back(static_cast<uint8_t>(value >> 24));
  buf.push_back(static_cast<uint8_t>(value >> 16));
  buf.push_back(static_cast<uint8_t>(value >> 8));
  buf.push_back(static_cast<uint8_t>(value));
}

uint32_t read_be32(const uint8_t* buf) {
  return (static_cast<uint32_t>(buf[0]) << 24) | (static_cast<uint32_t>(buf[1]) << 16) |
         (static_cast<uint32_t>(buf[2]) << 8) | static_cast<uint32_t>(buf[3]);
}

}  // namespace

discovery::Key32 derive_audio_key(const discovery::Key32& transport_key) {
  static constexpr char kLabel[] = "nockvm-audio-v1";
  std::vector<uint8_t> input(transport_key.begin(), transport_key.end());
  input.insert(input.end(), kLabel, kLabel + sizeof(kLabel) - 1);

  discovery::Key32 out{};
  crypto_generichash(out.data(), out.size(), input.data(), input.size(), nullptr, 0);
  return out;
}

std::vector<uint8_t> encode_frame(const discovery::Key32& key, uint32_t seq, const uint8_t* data, size_t len) {
  const std::vector<uint8_t> ciphertext = discovery::aead_encrypt(key, seq, nullptr, 0, data, len);

  std::vector<uint8_t> out;
  out.reserve(4 + ciphertext.size());
  write_be32(out, seq);
  out.insert(out.end(), ciphertext.begin(), ciphertext.end());
  return out;
}

bool decode_frame(const discovery::Key32& key, const uint8_t* data, size_t len, uint32_t& seq,
                   std::vector<uint8_t>& data_out) {
  if (len < 4) return false;
  seq = read_be32(data);
  return discovery::aead_decrypt(key, seq, nullptr, 0, data + 4, len - 4, data_out);
}

}  // namespace nockvm::audio
