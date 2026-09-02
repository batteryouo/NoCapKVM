#include "nockvm/discovery/tcp_client.h"
#include <chrono>
#include "nockvm/discovery/audio_port_protocol.h"
#include "nockvm/discovery/monitor_protocol.h"
#include "nockvm/discovery/noise_ik.h"
#include "nockvm/discovery/pairing.h"
#include "nockvm/discovery/platform_socket.h"
#include "nockvm/discovery/protocol.h"
#include "nockvm/discovery/secure_channel.h"
#include "nockvm/discovery/static_keys.h"
#include "nockvm/display/monitor_info.h"
#include "nockvm/input/inject.h"
#include "nockvm/input/protocol.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace nockvm::discovery {
namespace {
constexpr auto kHandshakeTimeout = std::chrono::seconds(3);
// How often a heartbeat is sent during an otherwise-idle connection. Well
// under any sane give_up_after_ so a handful of heartbeats can go missing
// before the connection is actually declared dead.
constexpr auto kHeartbeatInterval = std::chrono::milliseconds(1000);

// This machine is always the Slave role when it's the TcpClient side, so
// every input message received here is meant to be injected locally.
void dispatch_input_message(uint8_t msg_type, const std::vector<uint8_t>& payload) {
  using namespace input;
  switch (msg_type) {
    case kMsgMouseAbsolute: {
      int32_t x, y;
      if (decode_mouse_absolute(payload.data(), payload.size(), x, y)) inject_mouse_absolute(x, y);
      break;
    }
    case kMsgMouseButton: {
      uint8_t button;
      bool down;
      if (decode_mouse_button(payload.data(), payload.size(), button, down)) inject_mouse_button(button, down);
      break;
    }
    case kMsgMouseWheel: {
      int16_t delta;
      if (decode_mouse_wheel(payload.data(), payload.size(), delta)) inject_mouse_wheel(delta);
      break;
    }
    case kMsgKey: {
      uint32_t vk, scancode;
      bool down, extended;
      if (decode_key(payload.data(), payload.size(), vk, scancode, down, extended)) inject_key(vk, scancode, down, extended);
      break;
    }
    case kMsgModifierSync: {
      uint8_t mask;
      if (decode_modifier_sync(payload.data(), payload.size(), mask)) set_modifiers(mask);
      break;
    }
    default: break;
  }
}

}  // namespace

TcpClient::TcpClient(uint64_t own_device_id, std::string peer_ip, uint16_t peer_port, KnownPeers& known_peers,
                     std::chrono::milliseconds give_up_after)
    : own_device_id_(own_device_id),
      own_static_(get_or_create_static_keypair()),
      known_peers_(known_peers),
      peer_ip_(std::move(peer_ip)),
      peer_port_(peer_port),
      give_up_after_(give_up_after) {}

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

bool TcpClient::send_message(uint8_t msg_type, const uint8_t* payload, size_t len) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  if (!active_channel_) return false;
  return active_channel_->send(msg_type, payload, len);
}

TcpClient::AttemptOutcome TcpClient::run_once() {
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_ = ConnectionInfo{ConnectionState::Connecting, 0, peer_ip_, "", {}};
  }

  socket_t sock = create_tcp_socket();
  if (sock == kInvalidSocket) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = ConnectionState::Failed;
    return AttemptOutcome::kRetryBackoff;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(peer_port_);
  inet_pton(AF_INET, peer_ip_.c_str(), &addr.sin_addr);

  if (!connect_with_timeout(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr), kHandshakeTimeout)) {
    close_socket(sock);
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = ConnectionState::Failed;
    return AttemptOutcome::kRetryBackoff;
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
    return AttemptOutcome::kRetryBackoff;
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
    return AttemptOutcome::kRetryBackoff;
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
      return AttemptOutcome::kRetryBackoff;
    }

    const std::string fingerprint = compute_fingerprint(own_static_.public_key, peer_raw_pubkey);
    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_ = ConnectionInfo{ConnectionState::Pairing, peer_device_id, peer_ip_, fingerprint, {}};
    }

    uint8_t decision_byte = kPairingRejected;
    deadline = std::chrono::steady_clock::now() + kPairingApprovalTimeout;
    const bool decision_ok = recv_all(sock, &decision_byte, 1, deadline);
    if (!decision_ok) {
      close_socket(sock);
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_.state = ConnectionState::Failed;
      return AttemptOutcome::kRetryBackoff;
    }
    if (decision_byte != kPairingApproved) {
      // An explicit rejection, not a transient failure -- retrying would
      // just re-prompt Master for approval over and over. Give up for good.
      close_socket(sock);
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_.state = ConnectionState::Failed;
      return AttemptOutcome::kGiveUp;
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
    return AttemptOutcome::kRetryBackoff;
  }

  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_ = ConnectionInfo{ConnectionState::Connected, peer_device_id, peer_ip_, "", {}};
    status_.audio_key = ik.keys.send_key;  // the Slave-to-Master direction's key
  }

  SecureChannel channel(sock, ik.keys);
  {
    std::lock_guard<std::mutex> lock(send_mutex_);
    active_channel_ = &channel;
  }
  std::vector<display::MonitorInfo> last_sent_monitors = display::get_local_monitors();
  {
    const std::vector<uint8_t> monitor_payload = encode_monitor_list(last_sent_monitors);
    channel.send(kMsgMonitorList, monitor_payload.data(), monitor_payload.size());
  }

  auto last_heard = std::chrono::steady_clock::now();
  auto last_heartbeat_sent = std::chrono::steady_clock::now();
  auto last_monitor_check = std::chrono::steady_clock::now();
  while (running_.load()) {
    uint8_t msg_type;
    std::vector<uint8_t> payload;
    const auto result = channel.receive(msg_type, payload, std::chrono::steady_clock::now() + std::chrono::milliseconds(200));
    if (result == SecureChannel::RecvResult::Closed) break;
    if (result != SecureChannel::RecvResult::Timeout) last_heard = std::chrono::steady_clock::now();
    if (result == SecureChannel::RecvResult::Ok) {
      if (msg_type == kMsgAudioPort) {
        uint16_t audio_port = 0;
        if (decode_audio_port(payload.data(), payload.size(), audio_port)) {
          std::lock_guard<std::mutex> lock(status_mutex_);
          status_.peer_audio_port = audio_port;
        }
      } else if (msg_type == kMsgAudioControl) {
        bool send_enabled;
        uint32_t sample_rate;
        uint8_t bit_depth;
        if (decode_audio_settings(payload.data(), payload.size(), send_enabled, sample_rate, bit_depth)) {
          std::lock_guard<std::mutex> lock(status_mutex_);
          status_.master_requested_send_enabled = send_enabled;
          status_.master_requested_sample_rate = sample_rate;
          status_.master_requested_bit_depth = bit_depth;
          status_.master_audio_control_received = true;
          status_.master_audio_control_seq++;
        }
      } else if (msg_type == kMsgGoAway) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_.go_away_received = true;
      } else if (msg_type != kMsgHeartbeat) {
        dispatch_input_message(msg_type, payload);
      }
    }
    // Timeout/Error: keep polling running_.

    const auto now = std::chrono::steady_clock::now();
    if (now - last_heard >= give_up_after_) break;  // silent peer -- same as a closed connection
    if (now - last_heartbeat_sent >= kHeartbeatInterval) {
      uint8_t no_payload = 0;
      channel.send(kMsgHeartbeat, &no_payload, 0);
      last_heartbeat_sent = now;
    }
    // Polled rather than hooked into an OS hotplug event (WM_DISPLAYCHANGE,
    // XRandR, ...) -- simpler and platform-independent, and once a second
    // is plenty responsive for a monitor being plugged/unplugged. Without
    // this, Master would keep whatever monitor list Slave reported at
    // connect time forever, so e.g. unplugging Slave's display wouldn't
    // stop Master from still crossing the mouse onto it.
    if (now - last_monitor_check >= kHeartbeatInterval) {
      std::vector<display::MonitorInfo> current_monitors = display::get_local_monitors();
      if (current_monitors != last_sent_monitors) {
        const std::vector<uint8_t> monitor_payload = encode_monitor_list(current_monitors);
        channel.send(kMsgMonitorList, monitor_payload.data(), monitor_payload.size());
        last_sent_monitors = std::move(current_monitors);
      }
      last_monitor_check = now;
    }
  }

  {
    std::lock_guard<std::mutex> lock(send_mutex_);
    active_channel_ = nullptr;
  }
  close_socket(sock);
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.state = ConnectionState::Failed;
  }
  // Reached Connected before dropping -- retry soon rather than backing off,
  // since this looks like an external event (Master closed, emergency-escape
  // disconnect, a brief network blip) rather than the peer being unreachable.
  return AttemptOutcome::kRetryFast;
}

void TcpClient::run() {
  constexpr auto kInitialBackoff = std::chrono::milliseconds(1000);
  constexpr auto kMaxBackoff = std::chrono::milliseconds(10000);
  constexpr auto kBackoffPollInterval = std::chrono::milliseconds(200);

  auto backoff = kInitialBackoff;
  // Bounds one continuous stretch of "not connected" -- reset every time an
  // attempt actually reaches Connected (see below), so it never penalizes a
  // peer that's been up and down repeatedly over a long session, only a
  // single stretch of unreachability longer than give_up_after_.
  auto give_up_deadline = std::chrono::steady_clock::now() + give_up_after_;

  while (running_.load()) {
    const AttemptOutcome outcome = run_once();
    if (outcome == AttemptOutcome::kGiveUp || !running_.load()) break;

    if (outcome == AttemptOutcome::kRetryFast) {
      give_up_deadline = std::chrono::steady_clock::now() + give_up_after_;
      backoff = kInitialBackoff;
    } else if (std::chrono::steady_clock::now() >= give_up_deadline) {
      break;
    }

    // Sleep in short slices rather than one long sleep_for(backoff) so
    // stop() (triggered by the user's own Disconnect button) doesn't have
    // to wait out the full backoff before this thread notices and exits.
    // Also re-checked against give_up_deadline each slice so a long backoff
    // wait doesn't overshoot the configured timeout by much.
    auto remaining = backoff;
    while (remaining > std::chrono::milliseconds(0) && running_.load()) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= give_up_deadline) break;
      const auto step = std::min({remaining, kBackoffPollInterval,
                                   std::chrono::duration_cast<std::chrono::milliseconds>(give_up_deadline - now)});
      std::this_thread::sleep_for(step);
      remaining -= step;
    }

    if (std::chrono::steady_clock::now() >= give_up_deadline) break;
    if (outcome != AttemptOutcome::kRetryFast) backoff = std::min(backoff * 2, kMaxBackoff);
  }
}

}  // namespace nockvm::discovery
