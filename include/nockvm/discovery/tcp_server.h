#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_set>
#include "nockvm/discovery/connection_types.h"
#include "nockvm/discovery/known_peers.h"
#include "nockvm/discovery/noise_primitives.h"
#include "nockvm/discovery/platform_socket.h"

namespace nockvm::discovery {

class SecureChannel;

enum class PairingDecision : uint8_t { Pending, Approved, Rejected };

// Listens for a single incoming TCP client. On an unknown peer, runs TOFU
// pairing (fingerprint + Master-side approve/reject) before proceeding; on
// an already-trusted peer, skips straight to the Noise IK handshake. Then
// watches the connection until the peer disconnects, at which point it
// goes back to accepting.
class TcpServer {
public:
  // heartbeat_timeout bounds how long the current connection can go without
  // any traffic (real messages or the periodic heartbeat both count) before
  // it's treated as dead and force-disconnected -- the same policy as the
  // emergency-escape hotkey's manual disconnect_current(), just triggered
  // automatically instead of by a keypress. Live-adjustable afterward via
  // set_heartbeat_timeout() since this object outlives any single
  // connection (the user's Discovery-screen setting can change while
  // already connected).
  TcpServer(uint64_t own_device_id, KnownPeers& known_peers,
            std::chrono::milliseconds heartbeat_timeout = std::chrono::seconds(10));
  ~TcpServer();
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  // Binds an ephemeral port and starts listening synchronously so port()
  // is valid as soon as this returns, then spawns the accept-loop thread.
  void start();
  void stop();

  uint16_t port() const { return port_; }
  // The UDP port bound for the Slave-to-Master audio stream, valid as soon
  // as start() returns (bound alongside the TCP listen socket). The raw
  // socket is exposed too -- the app layer owns the actual AudioChannel/
  // AudioPlayback built on top of it, matching how input capture is owned
  // by the app layer rather than by TcpServer itself.
  uint16_t audio_port() const { return audio_port_; }
  socket_t audio_socket() const { return audio_socket_; }
  ConnectionInfo status() const;

  // Called from the UI thread to resolve a pending pairing request.
  void approve_pairing();
  void reject_pairing();

  // Called from the UI thread to end the current connection (if any)
  // without stopping the listener itself.
  void disconnect_current();

  // Called from the UI thread whenever the user changes the connection
  // timeout setting. Takes effect on the connection currently being served
  // (checked once per receive-loop iteration), not just future ones.
  void set_heartbeat_timeout(std::chrono::milliseconds timeout);

  // Called from the UI/input thread to send a message on the active
  // connection (e.g. input events). No-ops (returns false) if there is no
  // connection currently in the Connected state. Safe to call concurrently
  // with the background thread's own receive() loop — send/receive use
  // independent SecureChannel nonce counters, and this is the only caller
  // of send(), so there is no send/send race to guard against beyond
  // send_mutex_ serializing this method against itself.
  bool send_input(uint8_t msg_type, const uint8_t* payload, size_t len);

private:
  void run();

  uint64_t own_device_id_;
  Keypair own_static_;
  KnownPeers& known_peers_;
  socket_t listen_socket_ = kInvalidSocket;
  uint16_t port_ = 0;
  socket_t audio_socket_ = kInvalidSocket;
  uint16_t audio_port_ = 0;
  std::atomic<bool> running_{false};
  std::atomic<PairingDecision> pairing_decision_{PairingDecision::Pending};
  std::atomic<bool> disconnect_requested_{false};
  std::atomic<int64_t> heartbeat_timeout_ms_;
  // Session-scoped only (never persisted, unrelated to known_peers_'s
  // permanent TOFU trust): device IDs Master itself disconnected on
  // purpose. A device in here is still fully trusted, but must be
  // re-approved (see run()'s post-handshake gate) rather than silently
  // let back in, until that approval actually happens. Only ever touched
  // from run()'s own thread, so no locking needed.
  std::unordered_set<uint64_t> kicked_this_session_;
  std::thread thread_;
  mutable std::mutex status_mutex_;
  ConnectionInfo status_;
  mutable std::mutex send_mutex_;
  SecureChannel* active_channel_ = nullptr;  // set while Connected, guarded by send_mutex_
};

}  // namespace nockvm::discovery
