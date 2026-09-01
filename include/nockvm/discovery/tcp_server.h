#pragma once
#include <atomic>
#include <mutex>
#include <thread>
#include "nockvm/discovery/connection_types.h"
#include "nockvm/discovery/platform_socket.h"

namespace nockvm::discovery {

// Listens for a single incoming TCP client, exchanges device ids as a
// minimal handshake (no encryption), then watches the connection until
// the peer disconnects, at which point it goes back to accepting.
class TcpServer {
public:
  explicit TcpServer(uint64_t own_device_id);
  ~TcpServer();
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;

  // Binds an ephemeral port and starts listening synchronously so port()
  // is valid as soon as this returns, then spawns the accept-loop thread.
  void start();
  void stop();

  uint16_t port() const { return port_; }
  ConnectionInfo status() const;

private:
  void run();

  uint64_t own_device_id_;
  socket_t listen_socket_ = kInvalidSocket;
  uint16_t port_ = 0;
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex status_mutex_;
  ConnectionInfo status_;
};

}  // namespace nockvm::discovery
