#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace nockvm::discovery {

using Key32 = std::array<uint8_t, 32>;

struct Keypair {
  Key32 public_key;
  Key32 private_key;
};

struct TransportKeys {
  Key32 send_key;
  Key32 recv_key;
};

// Generates a fresh X25519 keypair (crypto_box_keypair's raw pk/sk are
// directly usable X25519 keys, no separate clamping step needed).
Keypair generate_keypair();

// Raw X25519 scalar multiplication. Returns false if libsodium rejects the
// result as a degenerate/small-order point (e.g. a malformed peer key) —
// callers must check this, since these keys can come from the network.
bool dh(const Key32& own_private, const Key32& peer_public, Key32& out);

struct SymmetricState {
  Key32 h{};
  Key32 ck{};
  Key32 k{};
  bool has_key = false;
  uint64_t n = 0;
};

// h = HASH(protocol_name) (protocol_name is always > HASHLEN=32 for this
// cipher suite, so this is always the hash branch, never zero-padding).
void initialize_symmetric(SymmetricState& state, const char* protocol_name, size_t len);

// h = HASH(h || data).
void mix_hash(SymmetricState& state, const uint8_t* data, size_t len);

// (ck, temp_k) = HKDF(ck, input_key_material, 2); sets k = temp_k,
// has_key = true, and resets n = 0 — every call replaces the key and
// restarts its nonce counter together.
void mix_key(SymmetricState& state, const Key32& input_key_material);

// Encrypts (if has_key) with key=k, nonce=n++, ad=h, else passes the
// plaintext through unchanged; either way folds the ciphertext actually
// placed on the wire into h.
std::vector<uint8_t> encrypt_and_hash(SymmetricState& state, const uint8_t* plaintext, size_t len);

// Inverse of encrypt_and_hash. Returns false on AEAD authentication failure.
bool decrypt_and_hash(SymmetricState& state, const uint8_t* ciphertext, size_t len, std::vector<uint8_t>& out);

// (k1, k2) = HKDF(ck, "", 2). Caller assigns k1/k2 to send/recv per role.
void split(const SymmetricState& state, Key32& k1, Key32& k2);

// ChaCha20-Poly1305 IETF AEAD. Nonce = 4 zero bytes || little-endian
// 8-byte nonce_counter, per Noise's ChaChaPoly nonce spec.
std::vector<uint8_t> aead_encrypt(const Key32& key, uint64_t nonce_counter, const uint8_t* ad, size_t ad_len,
                                   const uint8_t* plaintext, size_t len);
bool aead_decrypt(const Key32& key, uint64_t nonce_counter, const uint8_t* ad, size_t ad_len,
                   const uint8_t* ciphertext, size_t len, std::vector<uint8_t>& out);

}  // namespace nockvm::discovery
