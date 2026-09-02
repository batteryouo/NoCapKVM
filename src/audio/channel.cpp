#include "nockvm/audio/channel.h"
#include <chrono>
#include "nockvm/audio/protocol.h"

namespace nockvm::audio {
namespace {

// Generous fixed headroom over the largest packet any currently-offered
// format combination produces (48kHz/16-bit, the biggest: 240 frames *
// 4 bytes/frame = 960 bytes, + 4-byte seq + 16-byte AEAD tag = 980 bytes)
// -- not derived from AudioFormat since the receiver doesn't necessarily
// know the sender's chosen format ahead of a packet arriving.
constexpr size_t kMaxPacketSize = 2048;

}  // namespace

AudioChannel::AudioChannel(socket_t sock, discovery::Key32 key) : sock_(sock), key_(key) {}

bool AudioChannel::send_to(const sockaddr_in& dest, const uint8_t* data, size_t len) {
  const std::vector<uint8_t> packet = encode_frame(key_, send_seq_++, data, len);
  return sendto(sock_, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
                reinterpret_cast<const sockaddr*>(&dest), sizeof(dest)) == static_cast<int>(packet.size());
}

bool AudioChannel::receive_nonblocking(uint32_t& seq, std::vector<uint8_t>& data_out) {
  if (!discovery::wait_readable(sock_, std::chrono::milliseconds(0))) return false;

  uint8_t buf[kMaxPacketSize];
  sockaddr_in from{};
#ifdef _WIN32
  int from_len = sizeof(from);
#else
  socklen_t from_len = sizeof(from);
#endif
  const int received =
      recvfrom(sock_, reinterpret_cast<char*>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &from_len);
  if (received <= 0) return false;

  return decode_frame(key_, buf, static_cast<size_t>(received), seq, data_out);
}

}  // namespace nockvm::audio
