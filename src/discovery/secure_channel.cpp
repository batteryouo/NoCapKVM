#include "nockvm/discovery/secure_channel.h"
#include "nockvm/discovery/platform_socket.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace nockvm::discovery {
namespace {

// Once a frame has started arriving, it's expected to complete almost
// immediately on a local LAN; this bounds a stalled/half-open peer rather
// than the normal per-poll wait (that's `deadline` in receive()).
constexpr auto kFrameTimeout = std::chrono::seconds(3);
// Sanity cap. 1MB comfortably covered every message type here until
// clipboard image sync (JPEG-encoded clipboard images, see
// nockvm::clipboard::encode_jpeg's own max_bytes budget) started producing
// payloads that occasionally need more room than that.
constexpr uint32_t kMaxFrameLen = 8u << 20;

// Reads exactly `len` bytes, distinguishing a clean peer-close (`closed`
// set to true) from a stall past `deadline`.
bool read_exact(socket_t sock, uint8_t* buf, size_t len, std::chrono::steady_clock::time_point deadline,
                 bool& closed) {
  size_t received = 0;
  while (received < len) {
    if (std::chrono::steady_clock::now() >= deadline) return false;
    const int n = recv(sock, reinterpret_cast<char*>(buf + received), static_cast<int>(len - received), 0);
    if (n > 0) {
      received += static_cast<size_t>(n);
    } else if (n == 0) {
      closed = true;
      return false;
    }
    // n < 0: likely a receive-timeout expiring; loop back and recheck the deadline.
  }
  return true;
}

}  // namespace

SecureChannel::SecureChannel(socket_t sock, TransportKeys keys) : sock_(sock), keys_(keys) {}

bool SecureChannel::send(uint8_t msg_type, const uint8_t* payload, size_t len) {
  std::vector<uint8_t> plaintext;
  plaintext.reserve(len + 1);
  plaintext.push_back(msg_type);
  plaintext.insert(plaintext.end(), payload, payload + len);

  const std::vector<uint8_t> ciphertext =
      aead_encrypt(keys_.send_key, send_nonce_, nullptr, 0, plaintext.data(), plaintext.size());
  send_nonce_++;

  const uint32_t ct_len = static_cast<uint32_t>(ciphertext.size());
  uint8_t len_buf[4] = {
      static_cast<uint8_t>(ct_len >> 24),
      static_cast<uint8_t>(ct_len >> 16),
      static_cast<uint8_t>(ct_len >> 8),
      static_cast<uint8_t>(ct_len),
  };

  return send_all(sock_, len_buf, sizeof(len_buf)) && send_all(sock_, ciphertext.data(), ciphertext.size());
}

SecureChannel::RecvResult SecureChannel::receive(uint8_t& msg_type, std::vector<uint8_t>& payload,
                                                   std::chrono::steady_clock::time_point deadline) {
  const auto now = std::chrono::steady_clock::now();
  const auto wait_ms =
      deadline > now ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now) : std::chrono::milliseconds(0);
  if (!wait_readable(sock_, wait_ms)) return RecvResult::Timeout;

  // Something is readable: commit to reading the whole frame, bounded by
  // its own generous deadline rather than the (typically short, ~200ms)
  // poll deadline above — otherwise a frame that starts arriving right as
  // the poll window closes would be abandoned mid-frame and desync the
  // stream for the next call.
  const auto frame_deadline = std::chrono::steady_clock::now() + kFrameTimeout;

  uint8_t len_buf[4];
  bool closed = false;
  if (!read_exact(sock_, len_buf, sizeof(len_buf), frame_deadline, closed)) {
    return closed ? RecvResult::Closed : RecvResult::Error;
  }
  const uint32_t ct_len = (static_cast<uint32_t>(len_buf[0]) << 24) | (static_cast<uint32_t>(len_buf[1]) << 16) |
                           (static_cast<uint32_t>(len_buf[2]) << 8) | static_cast<uint32_t>(len_buf[3]);
  if (ct_len == 0 || ct_len > kMaxFrameLen) return RecvResult::Error;

  std::vector<uint8_t> ciphertext(ct_len);
  if (!read_exact(sock_, ciphertext.data(), ct_len, frame_deadline, closed)) {
    return closed ? RecvResult::Closed : RecvResult::Error;
  }

  std::vector<uint8_t> plaintext;
  if (!aead_decrypt(keys_.recv_key, recv_nonce_, nullptr, 0, ciphertext.data(), ciphertext.size(), plaintext)) {
    return RecvResult::Error;
  }
  recv_nonce_++;
  if (plaintext.empty()) return RecvResult::Error;

  msg_type = plaintext[0];
  payload.assign(plaintext.begin() + 1, plaintext.end());
  return RecvResult::Ok;
}

}  // namespace nockvm::discovery
