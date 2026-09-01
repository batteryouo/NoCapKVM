#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include "nockvm/discovery/connection_types.h"
#include "nockvm/discovery/known_peers.h"
#include "nockvm/discovery/noise_primitives.h"

namespace nockvm::discovery {

// Connects out to a single TCP server. On an unknown peer, runs TOFU
// pairing (fingerprint display, waits for the Master's approve/reject
// decision) before proceeding; on an already-trusted peer, skips straight
// to the Noise IK handshake. Then watches the connection until it drops
// or stop() is called.
class TcpClient {
public:
  TcpClient(uint64_t own_device_id, std::string peer_ip, uint16_t peer_port, KnownPeers& known_peers);
  ~TcpClient();
  TcpClient(const TcpClient&) = delete;
  TcpClient& operator=(const TcpClient&) = delete;

  void start();
  void stop();

  ConnectionInfo status() const;

private:
  void run();

  uint64_t own_device_id_;
  Keypair own_static_;
  KnownPeers& known_peers_;
  std::string peer_ip_;
  uint16_t peer_port_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex status_mutex_;
  ConnectionInfo status_;
};

}  // namespace nockvm::discovery
