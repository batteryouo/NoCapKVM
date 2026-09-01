#include "nockvm/discovery/noise_primitives.h"
#include <cstring>
#include <sodium.h>

namespace nockvm::discovery {
namespace {

void ensure_init() {
  static const int result = sodium_init();
  (void)result;
}

// Noise's own two-output HKDF (not RFC 5869's general form): temp_key =
// HMAC(chaining_key, ikm); out1 = HMAC(temp_key, [0x01]); out2 =
// HMAC(temp_key, out1 || [0x02]).
void hkdf2(const Key32& chaining_key, const uint8_t* ikm, size_t ikm_len, Key32& out1, Key32& out2) {
  Key32 temp_key;
  crypto_auth_hmacsha256(temp_key.data(), ikm, ikm_len, chaining_key.data());

  const uint8_t byte1 = 0x01;
  crypto_auth_hmacsha256(out1.data(), &byte1, 1, temp_key.data());

  uint8_t buf2[33];
  std::memcpy(buf2, out1.data(), 32);
  buf2[32] = 0x02;
  crypto_auth_hmacsha256(out2.data(), buf2, sizeof(buf2), temp_key.data());
}

void nonce_bytes(uint64_t nonce_counter, uint8_t out[12]) {
  std::memset(out, 0, 4);
  for (int i = 0; i < 8; ++i) out[4 + i] = static_cast<uint8_t>(nonce_counter >> (8 * i));
}

}  // namespace

Keypair generate_keypair() {
  ensure_init();
  Keypair kp;
  crypto_box_keypair(kp.public_key.data(), kp.private_key.data());
  return kp;
}

bool dh(const Key32& own_private, const Key32& peer_public, Key32& out) {
  ensure_init();
  return crypto_scalarmult_curve25519(out.data(), own_private.data(), peer_public.data()) == 0;
}

void initialize_symmetric(SymmetricState& state, const char* protocol_name, size_t len) {
  ensure_init();
  if (len <= state.h.size()) {
    // Per spec: name <= HASHLEN bytes is zero-padded, not hashed.
    state.h.fill(0);
    std::memcpy(state.h.data(), protocol_name, len);
  } else {
    crypto_hash_sha256(state.h.data(), reinterpret_cast<const uint8_t*>(protocol_name), len);
  }
  state.ck = state.h;
  state.k = Key32{};
  state.has_key = false;
  state.n = 0;
}

void mix_hash(SymmetricState& state, const uint8_t* data, size_t len) {
  std::vector<uint8_t> buf(state.h.begin(), state.h.end());
  buf.insert(buf.end(), data, data + len);
  crypto_hash_sha256(state.h.data(), buf.data(), buf.size());
}

void mix_key(SymmetricState& state, const Key32& input_key_material) {
  Key32 new_ck, temp_k;
  hkdf2(state.ck, input_key_material.data(), input_key_material.size(), new_ck, temp_k);
  state.ck = new_ck;
  state.k = temp_k;
  state.has_key = true;
  state.n = 0;
}

std::vector<uint8_t> aead_encrypt(const Key32& key, uint64_t nonce_counter, const uint8_t* ad, size_t ad_len,
                                   const uint8_t* plaintext, size_t len) {
  ensure_init();
  uint8_t nonce[12];
  nonce_bytes(nonce_counter, nonce);
  std::vector<uint8_t> out(len + crypto_aead_chacha20poly1305_ietf_ABYTES);
  unsigned long long out_len = 0;
  crypto_aead_chacha20poly1305_ietf_encrypt(out.data(), &out_len, plaintext, len, ad, ad_len, nullptr, nonce,
                                             key.data());
  out.resize(out_len);
  return out;
}

bool aead_decrypt(const Key32& key, uint64_t nonce_counter, const uint8_t* ad, size_t ad_len,
                   const uint8_t* ciphertext, size_t len, std::vector<uint8_t>& out) {
  ensure_init();
  uint8_t nonce[12];
  nonce_bytes(nonce_counter, nonce);
  out.resize(len);
  unsigned long long out_len = 0;
  if (crypto_aead_chacha20poly1305_ietf_decrypt(out.data(), &out_len, nullptr, ciphertext, len, ad, ad_len, nonce,
                                                 key.data()) != 0) {
    return false;
  }
  out.resize(out_len);
  return true;
}

std::vector<uint8_t> encrypt_and_hash(SymmetricState& state, const uint8_t* plaintext, size_t len) {
  std::vector<uint8_t> ct;
  if (state.has_key) {
    ct = aead_encrypt(state.k, state.n, state.h.data(), state.h.size(), plaintext, len);
    state.n++;
  } else {
    ct.assign(plaintext, plaintext + len);
  }
  mix_hash(state, ct.data(), ct.size());
  return ct;
}

bool decrypt_and_hash(SymmetricState& state, const uint8_t* ciphertext, size_t len, std::vector<uint8_t>& out) {
  if (state.has_key) {
    if (!aead_decrypt(state.k, state.n, state.h.data(), state.h.size(), ciphertext, len, out)) return false;
    state.n++;
  } else {
    out.assign(ciphertext, ciphertext + len);
  }
  mix_hash(state, ciphertext, len);
  return true;
}

void split(const SymmetricState& state, Key32& k1, Key32& k2) {
  const uint8_t empty[1] = {0};
  hkdf2(state.ck, empty, 0, k1, k2);
}

}  // namespace nockvm::discovery
