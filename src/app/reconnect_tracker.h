#pragma once
#include <cstdint>

namespace nockvm::app {

// Counts a reconnect only when the connection transitions into "connected"
// after at least one earlier successful connection this process -- the
// very first successful connection never counts. Pure state machine, no
// clock reads or I/O; callers own ever_connected/reconnect_count (mirrors
// RetryBackoff's shape) and may call this every frame -- it is a no-op
// whenever was_connected == is_connected, so it only fires on the actual
// false-to-true edge.
inline void track_reconnect(bool was_connected, bool is_connected, bool& ever_connected, uint32_t& reconnect_count) {
  if (was_connected || !is_connected) return;
  if (ever_connected) ++reconnect_count;
  ever_connected = true;
}

}  // namespace nockvm::app
