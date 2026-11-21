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

  // - Timeout: no bytes arrived before the deadline.
  // - InProgress: bytes of a frame arrived, but not the whole frame yet. The
  //   peer is alive; call receive() again to continue the same frame.
  // - Closed: the peer closed or reset the connection.
  // - Error: the stream is no longer usable. A frame stalled midway, had an
  //   invalid length, or failed to decrypt, so the next bytes cannot be
  //   trusted to start a frame. The caller must drop the connection. Once
  //   receive() returns Error, every later call returns Error too.
  // - Ok: one complete frame is in msg_type and payload.
  enum class RecvResult { Timeout, InProgress, Closed, Error, Ok };

  // Reads until one frame completes or `deadline` passes, whichever comes
  // first, so it never blocks the caller much past `deadline`. A partial
  // frame is kept and resumed by the next call. See kFrameIdleTimeout in
  // secure_channel.cpp for the mid-frame stall limit.
  RecvResult receive(uint8_t& msg_type, std::vector<uint8_t>& payload, std::chrono::steady_clock::time_point deadline);

private:
  RecvResult fail();

  bool broken_ = false;
  // The frame being received: its 4-byte length prefix, then its body.
  uint8_t header_[4] = {};
  size_t header_got_ = 0;
  std::vector<uint8_t> body_;
  size_t body_got_ = 0;
  std::chrono::steady_clock::time_point last_progress_{};
  socket_t sock_;
  TransportKeys keys_;
  uint64_t send_nonce_ = 0;
  uint64_t recv_nonce_ = 0;
};

}  // namespace nockvm::discovery
