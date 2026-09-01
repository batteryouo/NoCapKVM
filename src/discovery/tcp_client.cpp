#include "nockvm/discovery/tcp_client.h"
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
namespace {
constexpr auto kHandshakeTimeout = std::chrono::seconds(3);
}  // namespace

TcpClient::TcpClient(uint64_t own_device_id, std::string peer_ip, uint16_t peer_port)
    : own_device_id_(own_device_id), peer_ip_(std::move(peer_ip)), peer_port_(peer_port) {}

TcpClient::~TcpClient() { stop(); }

void TcpClient::start() {
  if (running_.exchange(true)) return;
  thread_ = std::thread(&TcpClient::run, this);
}

void TcpClient::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}

ConnectionInfo TcpClient::status() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_;
}

void TcpClient::run() {
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_ = ConnectionInfo{ConnectionState::Connecting, 0, peer_ip_};
  }

  socket_t sock = create_tcp_socket();
  if (sock == kInvalidSocket) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = ConnectionState::Failed;
    return;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(peer_port_);
  inet_pton(AF_INET, peer_ip_.c_str(), &addr.sin_addr);

  if (connect(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
    close_socket(sock);
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = ConnectionState::Failed;
    return;
  }

  set_receive_timeout(sock, std::chrono::milliseconds(200));

  const DeviceIdBytes own_bytes = encode_device_id(own_device_id_);
  DeviceIdBytes peer_bytes{};
  const auto deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
  const bool handshake_ok = send_all(sock, own_bytes.data(), own_bytes.size()) &&
                             recv_all(sock, peer_bytes.data(), peer_bytes.size(), deadline);
  if (!handshake_ok) {
    close_socket(sock);
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = ConnectionState::Failed;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_ = ConnectionInfo{ConnectionState::Connected, decode_device_id(peer_bytes), peer_ip_};
  }

  while (running_.load()) {
    uint8_t watch_byte;
    const int n = recv(sock, reinterpret_cast<char*>(&watch_byte), 1, 0);
    if (n == 0) break;  // peer closed
  }

  close_socket(sock);
  std::lock_guard<std::mutex> lock(status_mutex_);
  status_.state = ConnectionState::Failed;
}

}  // namespace nockvm::discovery
