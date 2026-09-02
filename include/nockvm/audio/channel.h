#pragma once
#include <cstdint>
#include <vector>
#include "nockvm/discovery/noise_primitives.h"
#include "nockvm/discovery/platform_socket.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif

namespace nockvm::audio {

// Thin UDP wrapper mirroring nockvm::discovery::SecureChannel's shape, but
// for this module's own framing (see protocol.h) -- doesn't own the
// socket, same convention SecureChannel already follows (the caller
// creates and closes it).
class AudioChannel {
public:
  AudioChannel(socket_t sock, discovery::Key32 key);

  bool send_to(const sockaddr_in& dest, const int16_t* samples, size_t count);

  // Non-blocking. False if nothing is available right now, or if the
  // packet failed to decode (malformed, wrong key, or a genuine AEAD
  // authentication failure) -- deliberately not distinguished, since
  // either way there's nothing usable to hand back.
  bool receive_nonblocking(uint32_t& seq, std::vector<int16_t>& samples_out);

private:
  socket_t sock_;
  discovery::Key32 key_;
  uint32_t send_seq_ = 0;
};

}  // namespace nockvm::audio
