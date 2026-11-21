#include "nockvm/discovery/secure_channel.h"
#include <algorithm>
#include "nockvm/discovery/platform_socket.h"
#include "nockvm/telemetry/counters.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace nockvm::discovery {
namespace {

// Once a frame has started arriving, the longest gap allowed between two
// received chunks of it. This bounds a stalled or half-open peer, not the
// whole frame: a multi-megabyte clipboard image on a slow link may take
// longer than this in total, and must not fail as long as it keeps moving.
// Measured across receive() calls.
constexpr auto kFrameIdleTimeout = std::chrono::seconds(3);
// Sanity cap. 1MB comfortably covered every message type here until
// clipboard image sync (JPEG-encoded clipboard images, see
// nockvm::clipboard::encode_jpeg's own max_bytes budget) started producing
// payloads that occasionally need more room than that.
constexpr uint32_t kMaxFrameLen = 8u << 20;

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

  // One write per frame: with TCP_NODELAY, a separate write for the length
  // prefix would go out as its own packet.
  const uint32_t ct_len = static_cast<uint32_t>(ciphertext.size());
  std::vector<uint8_t> frame;
  frame.reserve(4 + ciphertext.size());
  frame.push_back(static_cast<uint8_t>(ct_len >> 24));
  frame.push_back(static_cast<uint8_t>(ct_len >> 16));
  frame.push_back(static_cast<uint8_t>(ct_len >> 8));
  frame.push_back(static_cast<uint8_t>(ct_len));
  frame.insert(frame.end(), ciphertext.begin(), ciphertext.end());
  return send_all(sock_, frame.data(), frame.size());
}

SecureChannel::RecvResult SecureChannel::fail() {
  if (!broken_) telemetry::counters().channel_errors.fetch_add(1, std::memory_order_relaxed);
  broken_ = true;
  return RecvResult::Error;
}

SecureChannel::RecvResult SecureChannel::receive(uint8_t& msg_type, std::vector<uint8_t>& payload,
                                                   std::chrono::steady_clock::time_point deadline) {
  using clock = std::chrono::steady_clock;
  if (broken_) return RecvResult::Error;

  // Reads whatever arrives until `deadline`. A frame that is not complete
  // by then stays buffered in this object and the next call resumes it, so
  // the caller's loop keeps running (heartbeats, stop flags) during a long
  // frame. The stream is only abandoned when a started frame stalls for
  // kFrameIdleTimeout, which leaves it misaligned and so is fatal.
  bool progressed = false;
  for (;;) {
    const auto now = clock::now();
    // Checked before reading, not only when the socket goes quiet: a socket
    // that stays readable (a long frame on a busy link) must not keep this
    // call past its deadline. At least one read happens per call, so even a
    // deadline already in the past makes progress.
    if (progressed && now >= deadline) return RecvResult::InProgress;
    const bool in_frame = header_got_ > 0;
    if (in_frame && now - last_progress_ >= kFrameIdleTimeout) return fail();

    auto until = deadline;
    if (in_frame) until = (std::min)(until, last_progress_ + kFrameIdleTimeout);  // parenthesized: windows.h min macro
    const auto wait = until > now ? std::chrono::ceil<std::chrono::milliseconds>(until - now) : std::chrono::milliseconds(0);
    if (!wait_readable(sock_, wait)) {
      if (clock::now() >= deadline) return progressed ? RecvResult::InProgress : RecvResult::Timeout;
      continue;  // the idle limit came first; the check above fails the frame
    }

    uint8_t* dst;
    size_t want;
    if (header_got_ < sizeof(header_)) {
      dst = header_ + header_got_;
      want = sizeof(header_) - header_got_;
    } else {
      dst = body_.data() + body_got_;
      want = body_.size() - body_got_;
    }
    const int n = recv(sock_, reinterpret_cast<char*>(dst), static_cast<int>(want), 0);
    // Readable but nothing to read is a peer close; a failure right after
    // the socket reported readable is a reset. Either way the session ends.
    if (n <= 0) return RecvResult::Closed;
    progressed = true;
    last_progress_ = clock::now();

    if (header_got_ < sizeof(header_)) {
      header_got_ += static_cast<size_t>(n);
      if (header_got_ < sizeof(header_)) continue;
      const uint32_t ct_len = (static_cast<uint32_t>(header_[0]) << 24) | (static_cast<uint32_t>(header_[1]) << 16) |
                              (static_cast<uint32_t>(header_[2]) << 8) | static_cast<uint32_t>(header_[3]);
      if (ct_len == 0 || ct_len > kMaxFrameLen) return fail();
      body_.assign(ct_len, 0);
      body_got_ = 0;
      continue;
    }
    body_got_ += static_cast<size_t>(n);
    if (body_got_ < body_.size()) continue;

    // Frame complete. Reset first, so the next call starts a new frame.
    const std::vector<uint8_t> ciphertext = std::move(body_);
    body_ = {};
    body_got_ = 0;
    header_got_ = 0;

    std::vector<uint8_t> plaintext;
    if (!aead_decrypt(keys_.recv_key, recv_nonce_, nullptr, 0, ciphertext.data(), ciphertext.size(), plaintext)) {
      return fail();
    }
    recv_nonce_++;
    if (plaintext.empty()) return fail();

    msg_type = plaintext[0];
    payload.assign(plaintext.begin() + 1, plaintext.end());
    return RecvResult::Ok;
  }
}

}  // namespace nockvm::discovery
