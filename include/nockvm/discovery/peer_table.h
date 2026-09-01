#pragma once
#include <mutex>
#include <unordered_map>
#include <vector>
#include "nockvm/discovery/types.h"

namespace nockvm::discovery {

class PeerTable {
public:
  void upsert(const PeerInfo& peer);
  void prune(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::duration max_age);
  std::vector<PeerInfo> snapshot() const;

private:
  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, PeerInfo> peers_;
};

}  // namespace nockvm::discovery
