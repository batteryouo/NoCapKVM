#pragma once
#include <atomic>
#include <thread>
#include "nockvm/discovery/peer_table.h"
#include "nockvm/discovery/types.h"

namespace nockvm::discovery {

// Listens for announce packets on a background thread and maintains a
// peer table, skipping announcements from own_device_id.
class Listener {
public:
  explicit Listener(uint64_t own_device_id);
  ~Listener();
  Listener(const Listener&) = delete;
  Listener& operator=(const Listener&) = delete;

  void start();
  void stop();
  std::vector<PeerInfo> peers() const;

private:
  void run();

  uint64_t own_device_id_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  PeerTable peer_table_;
};

}  // namespace nockvm::discovery
