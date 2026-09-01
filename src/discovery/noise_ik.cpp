#include "nockvm/discovery/noise_ik.h"
#include <cstring>

namespace nockvm::discovery {
namespace {

// 32 bytes == HASHLEN for SHA256, so initialize_symmetric takes the
// zero-pad branch (h = name padded with zeros), not the hash branch.
constexpr char kProtocolName[] = "Noise_IK_25519_ChaChaPoly_SHA256";
constexpr size_t kProtocolNameLen = sizeof(kProtocolName) - 1;

constexpr size_t kMsg1Size = 96;  // e.pub(32) || EncryptAndHash(s.pub)(48) || EncryptAndHash("")(16)
constexpr size_t kMsg2Size = 48;  // e.pub(32) || EncryptAndHash("")(16)

const uint8_t kEmptyPayload[1] = {0};

}  // namespace

IkResult run_ik_initiator(socket_t sock, const Keypair& own_static, const Key32& peer_static,
                           std::chrono::steady_clock::time_point deadline) {
  IkResult result;
  SymmetricState state;
  initialize_symmetric(state, kProtocolName, kProtocolNameLen);
  mix_hash(state, peer_static.data(), peer_static.size());  // pre-message: responder's static

  const Keypair e = generate_keypair();
  mix_hash(state, e.public_key.data(), e.public_key.size());

  Key32 es;
  if (!dh(e.private_key, peer_static, es)) return result;
  mix_key(state, es);

  const std::vector<uint8_t> ct_s = encrypt_and_hash(state, own_static.public_key.data(), own_static.public_key.size());

  Key32 ss;
  if (!dh(own_static.private_key, peer_static, ss)) return result;
  mix_key(state, ss);

  const std::vector<uint8_t> ct_payload = encrypt_and_hash(state, kEmptyPayload, 0);

  std::vector<uint8_t> msg1;
  msg1.insert(msg1.end(), e.public_key.begin(), e.public_key.end());
  msg1.insert(msg1.end(), ct_s.begin(), ct_s.end());
  msg1.insert(msg1.end(), ct_payload.begin(), ct_payload.end());
  if (!send_all(sock, msg1.data(), msg1.size())) return result;

  uint8_t msg2[kMsg2Size];
  if (!recv_all(sock, msg2, sizeof(msg2), deadline)) return result;

  Key32 re2;
  std::memcpy(re2.data(), msg2, 32);
  mix_hash(state, re2.data(), re2.size());

  Key32 ee;
  if (!dh(e.private_key, re2, ee)) return result;
  mix_key(state, ee);

  Key32 se;
  if (!dh(own_static.private_key, re2, se)) return result;
  mix_key(state, se);

  std::vector<uint8_t> payload2;
  if (!decrypt_and_hash(state, msg2 + 32, 16, payload2)) return result;

  Key32 k1, k2;
  split(state, k1, k2);
  result.ok = true;
  result.keys.send_key = k1;
  result.keys.recv_key = k2;
  result.peer_static = peer_static;
  return result;
}

IkResult run_ik_responder(socket_t sock, const Keypair& own_static, std::chrono::steady_clock::time_point deadline) {
  IkResult result;
  SymmetricState state;
  initialize_symmetric(state, kProtocolName, kProtocolNameLen);
  mix_hash(state, own_static.public_key.data(), own_static.public_key.size());  // pre-message: own static

  uint8_t msg1[kMsg1Size];
  if (!recv_all(sock, msg1, sizeof(msg1), deadline)) return result;

  Key32 re;
  std::memcpy(re.data(), msg1, 32);
  mix_hash(state, re.data(), re.size());

  Key32 es;
  if (!dh(own_static.private_key, re, es)) return result;
  mix_key(state, es);

  std::vector<uint8_t> rs_bytes;
  if (!decrypt_and_hash(state, msg1 + 32, 48, rs_bytes)) return result;
  if (rs_bytes.size() != 32) return result;
  Key32 rs_initiator;
  std::memcpy(rs_initiator.data(), rs_bytes.data(), 32);

  Key32 ss;
  if (!dh(own_static.private_key, rs_initiator, ss)) return result;
  mix_key(state, ss);

  std::vector<uint8_t> payload1;
  if (!decrypt_and_hash(state, msg1 + 32 + 48, 16, payload1)) return result;

  const Keypair e2 = generate_keypair();
  mix_hash(state, e2.public_key.data(), e2.public_key.size());

  Key32 ee;
  if (!dh(e2.private_key, re, ee)) return result;
  mix_key(state, ee);

  Key32 se;
  if (!dh(e2.private_key, rs_initiator, se)) return result;
  mix_key(state, se);

  const std::vector<uint8_t> ct2 = encrypt_and_hash(state, kEmptyPayload, 0);

  std::vector<uint8_t> msg2;
  msg2.insert(msg2.end(), e2.public_key.begin(), e2.public_key.end());
  msg2.insert(msg2.end(), ct2.begin(), ct2.end());
  if (!send_all(sock, msg2.data(), msg2.size())) return result;

  Key32 k1, k2;
  split(state, k1, k2);
  result.ok = true;
  result.keys.send_key = k2;
  result.keys.recv_key = k1;
  result.peer_static = rs_initiator;
  return result;
}

}  // namespace nockvm::discovery
