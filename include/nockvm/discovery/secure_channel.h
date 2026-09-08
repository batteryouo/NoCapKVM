#pragma once
#include <chrono>
#include <cstdint>
#include <vector>
#include "nockvm/discovery/noise_primitives.h"
#include "nockvm/discovery/platform_socket.h"

namespace nockvm::discovery {

// Typed encrypted channel using length-prefixed [msg_type][payload] frames.
class SecureChannel {
public:
  SecureChannel(socket_t sock, TransportKeys keys);

  bool send(uint8_t msg_type, const uint8_t* payload, size_t len);

  enum class RecvResult { Timeout, Closed, Error, Ok };

  // Blocks (bounded by the socket's receive timeout, looped up to
  // `deadline`) for one full frame.
  RecvResult receive(uint8_t& msg_type, std::vector<uint8_t>& payload, std::chrono::steady_clock::time_point deadline);

private:
  socket_t sock_;
  TransportKeys keys_;
  uint64_t send_nonce_ = 0;
  uint64_t recv_nonce_ = 0;
};

}  // namespace nockvm::discovery
