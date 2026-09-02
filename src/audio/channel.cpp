#include "nockvm/audio/channel.h"
#include <chrono>
#include "nockvm/audio/format.h"
#include "nockvm/audio/protocol.h"

namespace nockvm::audio {
namespace {

// Plaintext for one packet is at most kSamplesPerPacket * 2 bytes; add
// generous headroom over the 4-byte seq + 16-byte AEAD tag for anything
// slightly larger than expected rather than silently truncating.
constexpr size_t kMaxPacketSize = kSamplesPerPacket * 2 + 64;

}  // namespace

AudioChannel::AudioChannel(socket_t sock, discovery::Key32 key) : sock_(sock), key_(key) {}

bool AudioChannel::send_to(const sockaddr_in& dest, const int16_t* samples, size_t count) {
  const std::vector<uint8_t> packet = encode_frame(key_, send_seq_++, samples, count);
  return sendto(sock_, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
                reinterpret_cast<const sockaddr*>(&dest), sizeof(dest)) == static_cast<int>(packet.size());
}

bool AudioChannel::receive_nonblocking(uint32_t& seq, std::vector<int16_t>& samples_out) {
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

  return decode_frame(key_, buf, static_cast<size_t>(received), seq, samples_out);
}

}  // namespace nockvm::audio
