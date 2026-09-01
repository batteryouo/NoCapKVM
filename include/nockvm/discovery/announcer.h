#pragma once
#include <atomic>
#include <string>
#include <thread>
#include "nockvm/discovery/types.h"

namespace nockvm::discovery {

// Periodically broadcasts an announce packet for this device on a background thread.
class Announcer {
public:
  Announcer(uint64_t device_id, Role role, uint16_t tcp_port, std::string hostname);
  ~Announcer();
  Announcer(const Announcer&) = delete;
  Announcer& operator=(const Announcer&) = delete;

  void start();
  void stop();

private:
  void run();

  uint64_t device_id_;
  Role role_;
  uint16_t tcp_port_;
  std::string hostname_;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace nockvm::discovery
