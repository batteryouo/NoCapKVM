#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace nockvm::topology {

enum class Direction : uint8_t { Left, Right, Up, Down };

struct ArrangementEntry {
  uint64_t device_id;
  Direction direction;  // peer's cluster sits in this direction relative to Master's cluster
  int32_t offset;       // peer cluster's origin along the perpendicular axis, in Master's coordinate space
};

// Master-local config: where each known peer's screen cluster sits relative
// to this machine's own, persisted as plain text under the config dir, one
// peer per line. Mirrors KnownPeers' load-in-constructor / save-on-mutation
// pattern.
class ScreenArrangement {
public:
  ScreenArrangement();
  std::optional<ArrangementEntry> get(uint64_t device_id) const;
  void set(uint64_t device_id, Direction direction, int32_t offset);
  void forget(uint64_t device_id);
  std::vector<ArrangementEntry> list() const;

private:
  void save() const;
  mutable std::mutex mutex_;
  std::unordered_map<uint64_t, ArrangementEntry> entries_;
};

}  // namespace nockvm::topology
