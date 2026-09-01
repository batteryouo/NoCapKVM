#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include "nockvm/discovery/connection_types.h"
#include "nockvm/discovery/known_peers.h"
#include "nockvm/discovery/noise_primitives.h"
#include "nockvm/discovery/platform_socket.h"

namespace nockvm::discovery {

enum class PairingDecision : uint8_t { Pending, Approved, Rejected };

// Listens for a single incoming TCP client. On an unknown peer, runs TOFU
// pairing (fingerprint + Master-side approve/reject) before proceeding; on
// an already-trusted peer, skips straight to the Noise IK handshake. Then
// watches the connection until the peer disconnects, at which point it
// goes back to accepting.
class TcpServer {
public:
  TcpServer(uint64_t own_device_id, KnownPeers& known_peers);
  ~TcpServer();
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  // Binds an ephemeral port and starts listening synchronously so port()
  // is valid as soon as this returns, then spawns the accept-loop thread.
  void start();
  void stop();

  uint16_t port() const { return port_; }
  ConnectionInfo status() const;

  // Called from the UI thread to resolve a pending pairing request.
  void approve_pairing();
  void reject_pairing();

  // Called from the UI thread to end the current connection (if any)
  // without stopping the listener itself.
  void disconnect_current();

private:
  void run();

  uint64_t own_device_id_;
  Keypair own_static_;
  KnownPeers& known_peers_;
  socket_t listen_socket_ = kInvalidSocket;
  uint16_t port_ = 0;
  std::atomic<bool> running_{false};
  std::atomic<PairingDecision> pairing_decision_{PairingDecision::Pending};
  std::atomic<bool> disconnect_requested_{false};
  std::thread thread_;
  mutable std::mutex status_mutex_;
  ConnectionInfo status_;
};

}  // namespace nockvm::discovery
