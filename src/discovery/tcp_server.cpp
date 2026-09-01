#include "nockvm/discovery/tcp_server.h"
#include <chrono>
#include "nockvm/discovery/protocol.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace nockvm::discovery {
namespace {
constexpr auto kHandshakeTimeout = std::chrono::seconds(3);
}  // namespace

TcpServer::TcpServer(uint64_t own_device_id) : own_device_id_(own_device_id) {}

TcpServer::~TcpServer() { stop(); }

void TcpServer::start() {
  if (running_.exchange(true)) return;

  listen_socket_ = create_tcp_socket();
  set_reuse_address(listen_socket_);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = INADDR_ANY;
  bind(listen_socket_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
  listen(listen_socket_, 1);

  sockaddr_in bound{};
#ifdef _WIN32
  int bound_len = sizeof(bound);
#else
  socklen_t bound_len = sizeof(bound);
#endif
  getsockname(listen_socket_, reinterpret_cast<sockaddr*>(&bound), &bound_len);
  port_ = ntohs(bound.sin_port);

  thread_ = std::thread(&TcpServer::run, this);
}

void TcpServer::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
  if (listen_socket_ != kInvalidSocket) {
    close_socket(listen_socket_);
    listen_socket_ = kInvalidSocket;
  }
}

ConnectionInfo TcpServer::status() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_;
}

void TcpServer::run() {
  while (running_.load()) {
    if (!wait_readable(listen_socket_, std::chrono::milliseconds(200))) continue;

    sockaddr_in peer_addr{};
#ifdef _WIN32
    int peer_len = sizeof(peer_addr);
#else
    socklen_t peer_len = sizeof(peer_addr);
#endif
    socket_t client = accept(listen_socket_, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len);
    if (client == kInvalidSocket) continue;

    set_receive_timeout(client, std::chrono::milliseconds(200));

    char ip_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_addr.sin_addr, ip_buf, sizeof(ip_buf));

    const DeviceIdBytes own_bytes = encode_device_id(own_device_id_);
    DeviceIdBytes peer_bytes{};
    const auto deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
    const bool handshake_ok = send_all(client, own_bytes.data(), own_bytes.size()) &&
                               recv_all(client, peer_bytes.data(), peer_bytes.size(), deadline);
    if (!handshake_ok) {
      close_socket(client);
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_ = ConnectionInfo{ConnectionState::Connected, decode_device_id(peer_bytes), ip_buf};
    }

    while (running_.load()) {
      uint8_t watch_byte;
      const int n = recv(client, reinterpret_cast<char*>(&watch_byte), 1, 0);
      if (n == 0) break;  // peer closed
    }

    close_socket(client);
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_ = ConnectionInfo{};
  }
}

}  // namespace nockvm::discovery
