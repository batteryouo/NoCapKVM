#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include "nockvm/discovery/noise_primitives.h"

namespace nockvm::discovery {

struct KnownPeerEntry {
  uint64_t device_id;
  Key32 pubkey;
};

// Local TOFU trust store: device_id -> trusted static public key,
// persisted as plain hex text under the config dir, one peer per line.
class KnownPeers {
public:
  KnownPeers();
  bool is_known(uint64_t device_id) const;
  std::optional<Key32> get_pubkey(uint64_t device_id) const;
  void remember(uint64_t device_id, const Key32& pubkey);
  void forget(uint64_t device_id);
  std::vector<KnownPeerEntry> list() const;

private:
  void save() const;
  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, Key32> peers_;
};

}  // namespace nockvm::discovery
