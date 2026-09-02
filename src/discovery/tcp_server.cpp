#include "nockvm/discovery/tcp_server.h"
#include <chrono>
#include <thread>
#include "nockvm/discovery/monitor_protocol.h"
#include "nockvm/discovery/noise_ik.h"
#include "nockvm/discovery/pairing.h"
#include "nockvm/discovery/protocol.h"
#include "nockvm/discovery/secure_channel.h"
#include "nockvm/discovery/static_keys.h"
#include "nockvm/display/monitor_info.h"

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

TcpServer::TcpServer(uint64_t own_device_id, KnownPeers& known_peers)
    : own_device_id_(own_device_id), own_static_(get_or_create_static_keypair()), known_peers_(known_peers) {}

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

void TcpServer::approve_pairing() { pairing_decision_ = PairingDecision::Approved; }
void TcpServer::reject_pairing() { pairing_decision_ = PairingDecision::Rejected; }
void TcpServer::disconnect_current() { disconnect_requested_ = true; }

bool TcpServer::send_input(uint8_t msg_type, const uint8_t* payload, size_t len) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  if (!active_channel_) return false;
  return active_channel_->send(msg_type, payload, len);
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
    auto deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
    const bool device_id_ok = send_all(client, own_bytes.data(), own_bytes.size()) &&
                               recv_all(client, peer_bytes.data(), peer_bytes.size(), deadline);
    if (!device_id_ok) {
      close_socket(client);
      continue;
    }
    const uint64_t peer_device_id = decode_device_id(peer_bytes);

    std::optional<Key32> trusted_pubkey = known_peers_.get_pubkey(peer_device_id);

    // Both sides must agree on whether pairing is needed before either one
    // acts on its own local known_peers state: otherwise one side (e.g.
    // after the other end forgot it) could skip straight into an IK
    // message while its peer is still expecting a raw pubkey exchange,
    // desyncing the byte stream.
    const uint8_t own_wants_pairing = trusted_pubkey.has_value() ? 0 : 1;
    uint8_t peer_wants_pairing = 1;
    deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
    const bool pairing_flag_ok = send_all(client, &own_wants_pairing, 1) &&
                                  recv_all(client, &peer_wants_pairing, 1, deadline);
    if (!pairing_flag_ok) {
      close_socket(client);
      continue;
    }
    const bool need_pairing = !trusted_pubkey.has_value() || peer_wants_pairing != 0;

    if (need_pairing) {
      Key32 peer_raw_pubkey;
      deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
      const bool pubkey_ok = send_all(client, own_static_.public_key.data(), own_static_.public_key.size()) &&
                              recv_all(client, peer_raw_pubkey.data(), peer_raw_pubkey.size(), deadline);
      if (!pubkey_ok) {
        close_socket(client);
        continue;
      }

      const std::string fingerprint = compute_fingerprint(peer_raw_pubkey, own_static_.public_key);
      {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_ = ConnectionInfo{ConnectionState::Pairing, peer_device_id, ip_buf, fingerprint, {}};
      }
      pairing_decision_ = PairingDecision::Pending;

      const auto approval_deadline = std::chrono::steady_clock::now() + kPairingApprovalTimeout;
      PairingDecision decision = PairingDecision::Pending;
      while (running_.load() && std::chrono::steady_clock::now() < approval_deadline) {
        decision = pairing_decision_.load();
        if (decision != PairingDecision::Pending) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      const uint8_t decision_byte = decision == PairingDecision::Approved ? kPairingApproved : kPairingRejected;
      send_all(client, &decision_byte, 1);
      if (decision != PairingDecision::Approved) {
        close_socket(client);
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_ = ConnectionInfo{};
        continue;
      }

      known_peers_.remember(peer_device_id, peer_raw_pubkey);
      trusted_pubkey = peer_raw_pubkey;
    }

    deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
    const IkResult ik = run_ik_responder(client, own_static_, deadline);
    if (!ik.ok || *trusted_pubkey != ik.peer_static) {
      close_socket(client);
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_ = ConnectionInfo{};
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_ = ConnectionInfo{ConnectionState::Connected, peer_device_id, ip_buf, "", {}};
    }

    SecureChannel channel(client, ik.keys);
    {
      std::lock_guard<std::mutex> lock(send_mutex_);
      active_channel_ = &channel;
    }
    disconnect_requested_ = false;
    while (running_.load() && !disconnect_requested_.load()) {
      uint8_t msg_type;
      std::vector<uint8_t> payload;
      const auto result =
          channel.receive(msg_type, payload, std::chrono::steady_clock::now() + std::chrono::milliseconds(200));
      if (result == SecureChannel::RecvResult::Closed) break;
      if (result == SecureChannel::RecvResult::Ok && msg_type == kMsgMonitorList) {
        std::vector<display::MonitorInfo> monitors;
        if (decode_monitor_list(payload.data(), payload.size(), monitors)) {
          std::lock_guard<std::mutex> lock(status_mutex_);
          status_.peer_monitors = std::move(monitors);
        }
      }
      // Timeout/Error: keep polling running_/disconnect_requested_.
    }

    {
      std::lock_guard<std::mutex> lock(send_mutex_);
      active_channel_ = nullptr;
    }
    close_socket(client);
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_ = ConnectionInfo{};
  }
}

}  // namespace nockvm::discovery
