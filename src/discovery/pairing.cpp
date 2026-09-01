#include "nockvm/discovery/pairing.h"
#include <cstdio>
#include <cstring>
#include <sodium.h>

namespace nockvm::discovery {

std::string compute_fingerprint(const Key32& initiator_pubkey, const Key32& responder_pubkey) {
  uint8_t buf[64];
  std::memcpy(buf, initiator_pubkey.data(), 32);
  std::memcpy(buf + 32, responder_pubkey.data(), 32);
  uint8_t digest[32];
  crypto_hash_sha256(digest, buf, sizeof(buf));
  char out[7];
  std::snprintf(out, sizeof(out), "%02x%02x%02x", digest[0], digest[1], digest[2]);
  return std::string(out);
}

}  // namespace nockvm::discovery
