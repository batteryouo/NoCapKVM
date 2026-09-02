#pragma once
#include <cstdint>
#include <vector>
#include "nockvm/discovery/noise_primitives.h"

namespace nockvm::audio {

// Derives a key for the audio UDP stream from the Noise session's already-
// established transport key. Deliberately a *different* key from the one
// SecureChannel uses for the TCP control channel -- reusing that key here
// with an independently-counting sequence number would be genuine nonce
// reuse (two different streams under the same key+counter space), not
// just a style inconsistency.
discovery::Key32 derive_audio_key(const discovery::Key32& transport_key);

// Packet framing: [4-byte BE seq][AEAD ciphertext+tag]. The sequence
// number rides in the clear ahead of the ciphertext (not used as
// associated data here -- these packets have no other header to bind it
// to) specifically because UDP doesn't preserve order, so the receiver
// needs it before it can even attempt decryption keyed on the matching
// nonce.
std::vector<uint8_t> encode_frame(const discovery::Key32& key, uint32_t seq, const int16_t* samples, size_t count);

// Returns false on a malformed packet or AEAD authentication failure.
bool decode_frame(const discovery::Key32& key, const uint8_t* data, size_t len, uint32_t& seq,
                   std::vector<int16_t>& samples_out);

}  // namespace nockvm::audio
