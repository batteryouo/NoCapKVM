#include "nockvm/audio/channel.h"
#include <chrono>
#include "nockvm/audio/protocol.h"
#include "nockvm/telemetry/counters.h"

namespace nockvm::audio {
namespace {

// Headroom over the largest packet any offered format produces (48kHz/16-bit:
// 240 frames * 4 bytes + 4-byte seq + 16-byte AEAD tag = 980 bytes), which
// AudioCapture guarantees by packetizing at bytes_per_packet(). Not derived
// from AudioFormat since the receiver learns the sender's format separately.
constexpr size_t kMaxPacketSize = 2048;

// Windows fails recvfrom() for a datagram larger than the buffer and
// discards it; POSIX truncates it instead, which decode_frame() rejects.
bool last_recv_was_oversized() {
#ifdef _WIN32
  return WSAGetLastError() == WSAEMSGSIZE;
#else
  return false;
#endif
}

}  // namespace

AudioChannel::AudioChannel(socket_t sock, discovery::Key32 key) : sock_(sock), key_(key) {}

bool AudioChannel::send_to(const sockaddr_in& dest, const uint8_t* data, size_t len) {
  const std::vector<uint8_t> packet = encode_frame(key_, send_seq_++, data, len);
  return sendto(sock_, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
                reinterpret_cast<const sockaddr*>(&dest), sizeof(dest)) == static_cast<int>(packet.size());
}

bool AudioChannel::receive_nonblocking(uint32_t& seq, std::vector<uint8_t>& data_out) {
  uint8_t buf[kMaxPacketSize];
  while (discovery::wait_readable(sock_, std::chrono::milliseconds(0))) {
    sockaddr_in from{};
#ifdef _WIN32
    int from_len = sizeof(from);
#else
    socklen_t from_len = sizeof(from);
#endif
    const int received =
        recvfrom(sock_, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
    if (received <= 0) {
      if (!last_recv_was_oversized()) return false;
      telemetry::counters().audio_packets_received.fetch_add(1, std::memory_order_relaxed);
      telemetry::counters().audio_packets_rejected.fetch_add(1, std::memory_order_relaxed);
      continue;
    }
    telemetry::counters().audio_packets_received.fetch_add(1, std::memory_order_relaxed);
    if (decode_frame(key_, buf, static_cast<size_t>(received), seq, data_out)) return true;
    telemetry::counters().audio_packets_rejected.fetch_add(1, std::memory_order_relaxed);
  }
  return false;
}

}  // namespace nockvm::audio
