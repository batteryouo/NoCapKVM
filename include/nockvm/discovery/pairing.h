#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include "nockvm/discovery/noise_primitives.h"

namespace nockvm::discovery {

constexpr uint8_t kPairingApproved = 0x01;
constexpr uint8_t kPairingRejected = 0x00;
// Deliberately much longer than the TCP handshake timeout: this wait spans
// a human looking at two screens and clicking, not a network round trip.
constexpr auto kPairingApprovalTimeout = std::chrono::minutes(2);

// SHA256(initiator_pubkey || responder_pubkey), first 3 bytes as 6
// lowercase hex chars. Role is structural (Slave is always the initiator,
// Master always the responder), so both sides compute this independently
// with no wire negotiation needed for the byte order.
std::string compute_fingerprint(const Key32& initiator_pubkey, const Key32& responder_pubkey);

}  // namespace nockvm::discovery
