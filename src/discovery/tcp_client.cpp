#include "nockvm/discovery/tcp_client.h"
#include <chrono>
#include "nockvm/discovery/monitor_protocol.h"
#include "nockvm/discovery/noise_ik.h"
#include "nockvm/discovery/pairing.h"
#include "nockvm/discovery/platform_socket.h"
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

TcpClient::TcpClient(uint64_t own_device_id, std::string peer_ip, uint16_t peer_port, KnownPeers& known_peers)
    : own_device_id_(own_device_id),
      own_static_(get_or_create_static_keypair()),
      known_peers_(known_peers),
      peer_ip_(std::move(peer_ip)),
      peer_port_(peer_port) {}

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
    status_ = ConnectionInfo{ConnectionState::Connecting, 0, peer_ip_, "", {}};
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
  auto deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
  const bool device_id_ok = send_all(sock, own_bytes.data(), own_bytes.size()) &&
                             recv_all(sock, peer_bytes.data(), peer_bytes.size(), deadline);
  if (!device_id_ok) {
    close_socket(sock);
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = ConnectionState::Failed;
    return;
  }
  const uint64_t peer_device_id = decode_device_id(peer_bytes);

  std::optional<Key32> rs = known_peers_.get_pubkey(peer_device_id);

  // Both sides must agree on whether pairing is needed before either one
  // acts on its own local known_peers state: otherwise one side (e.g.
  // after the other end forgot it) could skip straight into an IK message
  // while its peer is still expecting a raw pubkey exchange, desyncing
  // the byte stream.
  const uint8_t own_wants_pairing = rs.has_value() ? 0 : 1;
  uint8_t peer_wants_pairing = 1;
  deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
  const bool pairing_flag_ok =
      send_all(sock, &own_wants_pairing, 1) && recv_all(sock, &peer_wants_pairing, 1, deadline);
  if (!pairing_flag_ok) {
    close_socket(sock);
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = ConnectionState::Failed;
    return;
  }
  const bool need_pairing = !rs.has_value() || peer_wants_pairing != 0;

  if (need_pairing) {
    Key32 peer_raw_pubkey;
    deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
    const bool pubkey_ok = send_all(sock, own_static_.public_key.data(), own_static_.public_key.size()) &&
                            recv_all(sock, peer_raw_pubkey.data(), peer_raw_pubkey.size(), deadline);
    if (!pubkey_ok) {
      close_socket(sock);
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_.state = ConnectionState::Failed;
      return;
    }

    const std::string fingerprint = compute_fingerprint(own_static_.public_key, peer_raw_pubkey);
    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_ = ConnectionInfo{ConnectionState::Pairing, peer_device_id, peer_ip_, fingerprint, {}};
    }

    uint8_t decision_byte = kPairingRejected;
    deadline = std::chrono::steady_clock::now() + kPairingApprovalTimeout;
    const bool decision_ok = recv_all(sock, &decision_byte, 1, deadline);
    if (!decision_ok || decision_byte != kPairingApproved) {
      close_socket(sock);
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_.state = ConnectionState::Failed;
      return;
    }

    known_peers_.remember(peer_device_id, peer_raw_pubkey);
    rs = peer_raw_pubkey;
  }

  deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
  const IkResult ik = run_ik_initiator(sock, own_static_, *rs, deadline);
  if (!ik.ok) {
    close_socket(sock);
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = ConnectionState::Failed;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_ = ConnectionInfo{ConnectionState::Connected, peer_device_id, peer_ip_, "", {}};
  }

  SecureChannel channel(sock, ik.keys);
  const std::vector<uint8_t> monitor_payload = encode_monitor_list(display::get_local_monitors());
  channel.send(kMsgMonitorList, monitor_payload.data(), monitor_payload.size());

  while (running_.load()) {
    uint8_t msg_type;
    std::vector<uint8_t> payload;
    const auto result = channel.receive(msg_type, payload, std::chrono::steady_clock::now() + std::chrono::milliseconds(200));
    if (result == SecureChannel::RecvResult::Closed) break;
    // Timeout/Error: keep polling running_; Ok: nothing to dispatch on this side yet.
  }

  close_socket(sock);
  std::lock_guard<std::mutex> lock(status_mutex_);
  status_.state = ConnectionState::Failed;
}

}  // namespace nockvm::discovery
