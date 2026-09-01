#include "nockvm/discovery/listener.h"
#include <chrono>
#include "nockvm/discovery/platform_socket.h"
#include "nockvm/discovery/protocol.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace nockvm::discovery {

Listener::Listener(uint64_t own_device_id) : own_device_id_(own_device_id) {}

Listener::~Listener() { stop(); }

void Listener::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread(&Listener::run, this);
}

void Listener::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

std::vector<PeerInfo> Listener::peers() const { return peer_table_.snapshot(); }

void Listener::run() {
  socket_t sock = create_udp_socket();
  if (sock == kInvalidSocket) return;
  set_reuse_address(sock);
  set_receive_timeout(sock, std::chrono::milliseconds(200));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(kDiscoveryPort);
  addr.sin_addr.s_addr = INADDR_ANY;
  bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));

  uint8_t buf[kAnnouncePacketSize];
  while (running_.load()) {
    sockaddr_in from{};
#ifdef _WIN32
    int from_len = sizeof(from);
#else
    socklen_t from_len = sizeof(from);
#endif
    const int received = recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                                   reinterpret_cast<sockaddr*>(&from), &from_len);
    if (received > 0) {
      DecodedAnnounce decoded;
      if (decode_announce(buf, static_cast<size_t>(received), decoded) && decoded.device_id != own_device_id_) {
        char ip_buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip_buf, sizeof(ip_buf));
        PeerInfo peer;
        peer.device_id = decoded.device_id;
        peer.hostname = decoded.hostname;
        peer.role = decoded.role;
        peer.tcp_port = decoded.tcp_port;
        peer.ip_address = ip_buf;
        peer.last_seen = std::chrono::steady_clock::now();
        peer_table_.upsert(peer);
      }
    }
    peer_table_.prune(std::chrono::steady_clock::now(), kPeerTimeout);
  }

  close_socket(sock);
}

}  // namespace nockvm::discovery
