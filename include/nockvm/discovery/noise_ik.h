#pragma once
#include <chrono>
#include "nockvm/discovery/noise_primitives.h"
#include "nockvm/discovery/platform_socket.h"

namespace nockvm::discovery {

struct IkResult {
  bool ok = false;
  TransportKeys keys{};
  Key32 peer_static{};  // meaningful only if ok == true
};

// Initiator = the connecting Slave; peer_static is the responder's (Master's)
// static public key, already known from a prior pairing.
IkResult run_ik_initiator(socket_t sock, const Keypair& own_static, const Key32& peer_static,
                           std::chrono::steady_clock::time_point deadline);

// Responder = Master. Does not take a peer key as input: it learns the
// initiator's static public key during the handshake and reports it via
// IkResult::peer_static. A successful result only proves "some valid
// X25519 keypair completed the handshake" — callers must separately verify
// peer_static against their own trust store before treating the connection
// as authenticated.
IkResult run_ik_responder(socket_t sock, const Keypair& own_static, std::chrono::steady_clock::time_point deadline);

}  // namespace nockvm::discovery
