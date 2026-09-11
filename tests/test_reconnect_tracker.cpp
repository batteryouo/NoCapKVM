#include <cassert>
#include "reconnect_tracker.h"

using namespace nockvm::app;

int main() {
  // The very first successful connection never counts as a reconnect.
  {
    bool ever_connected = false;
    uint32_t reconnect_count = 0;
    track_reconnect(false, true, ever_connected, reconnect_count);
    assert(ever_connected);
    assert(reconnect_count == 0);
  }

  // A later transition back into Connected, after at least one earlier
  // successful connection, counts.
  {
    bool ever_connected = true;
    uint32_t reconnect_count = 0;
    track_reconnect(false, true, ever_connected, reconnect_count);
    assert(reconnect_count == 1);
    track_reconnect(false, true, ever_connected, reconnect_count);
    assert(reconnect_count == 2);
  }

  // Steady state (already connected, still connected) and disconnecting
  // are both no-ops -- only the false-to-true edge counts.
  {
    bool ever_connected = true;
    uint32_t reconnect_count = 0;
    track_reconnect(true, true, ever_connected, reconnect_count);
    assert(reconnect_count == 0);
    track_reconnect(true, false, ever_connected, reconnect_count);
    assert(reconnect_count == 0);
    track_reconnect(false, false, ever_connected, reconnect_count);
    assert(reconnect_count == 0);
  }

  // A full connect/disconnect/reconnect sequence from a cold start.
  {
    bool ever_connected = false;
    uint32_t reconnect_count = 0;
    track_reconnect(false, true, ever_connected, reconnect_count);   // first connect
    track_reconnect(true, false, ever_connected, reconnect_count);   // disconnect
    track_reconnect(false, true, ever_connected, reconnect_count);   // reconnect #1
    track_reconnect(true, false, ever_connected, reconnect_count);   // disconnect
    track_reconnect(false, true, ever_connected, reconnect_count);   // reconnect #2
    assert(reconnect_count == 2);
  }

  return 0;
}
