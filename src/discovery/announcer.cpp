#include "nockvm/discovery/announcer.h"
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

Announcer::Announcer(uint64_t device_id, Role role, uint16_t tcp_port, std::string hostname)
    : device_id_(device_id), role_(role), tcp_port_(tcp_port), hostname_(std::move(hostname)) {}

Announcer::~Announcer() { stop(); }

void Announcer::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread(&Announcer::run, this);
}

void Announcer::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

void Announcer::run() {
  socket_t sock = create_udp_socket();
  if (sock == kInvalidSocket) return;
  set_broadcast(sock);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(kDiscoveryPort);
  addr.sin_addr.s_addr = INADDR_BROADCAST;

  const AnnouncePacket packet = encode_announce(device_id_, role_, tcp_port_, hostname_);

  while (running_.load()) {
    sendto(sock, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
           reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    for (int i = 0; i < 10 && running_.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  close_socket(sock);
}

}  // namespace nockvm::discovery
