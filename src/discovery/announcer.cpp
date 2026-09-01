#include "nockvm/discovery/announcer.h"
#include <chrono>
#include <vector>
#include "nockvm/discovery/network_interfaces.h"
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

  const AnnouncePacket packet = encode_announce(device_id_, role_, tcp_port_, hostname_);

  while (running_.load()) {
    // Sent per-interface (rather than to the limited broadcast address 255.255.255.255)
    // because on a multi-homed machine the OS's routing table can pick an unrelated
    // interface for the limited broadcast, silently dropping the announcement off-LAN.
    std::vector<uint32_t> targets = get_broadcast_targets();
    if (targets.empty()) targets.push_back(INADDR_BROADCAST);

    for (uint32_t target : targets) {
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_port = htons(kDiscoveryPort);
      addr.sin_addr.s_addr = target;
      sendto(sock, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
             reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    }

    for (int i = 0; i < 10 && running_.load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  close_socket(sock);
}

}  // namespace nockvm::discovery
