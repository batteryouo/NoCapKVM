#include "nockvm/discovery/peer_table.h"

namespace nockvm::discovery {

void PeerTable::upsert(const PeerInfo& peer) {
  std::lock_guard<std::mutex> lock(mutex_);
  peers_[peer.device_id] = peer;
}

void PeerTable::prune(std::chrono::steady_clock::time_point now, std::chrono::steady_clock::duration max_age) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = peers_.begin(); it != peers_.end();) {
    if (now - it->second.last_seen > max_age) it = peers_.erase(it);
    else ++it;
  }
}

std::vector<PeerInfo> PeerTable::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<PeerInfo> result;
  result.reserve(peers_.size());
  for (const auto& [id, peer] : peers_) result.push_back(peer);
  return result;
}

}  // namespace nockvm::discovery
