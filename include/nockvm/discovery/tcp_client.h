#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include "nockvm/discovery/connection_types.h"

namespace nockvm::discovery {

// Connects out to a single TCP server, exchanges device ids as a
// minimal handshake (no encryption), then watches the connection until
// it drops or stop() is called.
class TcpClient {
public:
  TcpClient(uint64_t own_device_id, std::string peer_ip, uint16_t peer_port);
  ~TcpClient();
  TcpClient(const TcpClient&) = delete;
  TcpClient& operator=(const TcpClient&) = delete;

  void start();
  void stop();

  ConnectionInfo status() const;

private:
  void run();

  uint64_t own_device_id_;
  std::string peer_ip_;
  uint16_t peer_port_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex status_mutex_;
  ConnectionInfo status_;
};

}  // namespace nockvm::discovery
