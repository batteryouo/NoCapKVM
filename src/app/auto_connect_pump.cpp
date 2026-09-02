#include "auto_connect_pump.h"
#include <chrono>
#include "nockvm/discovery/connection_types.h"

namespace nockvm::app {

void pump_auto_connect(AppState& state) {
  if (state.role != discovery::Role::Slave) return;
  if (!state.auto_connect_enabled) return;
  if (!state.listener) return;  // discovery hasn't started yet

  if (state.tcp_client) {
    const auto s = state.tcp_client->status().state;
    // Already busy or connected -- nothing to do. Idle/Failed (gave up, or
    // errored out before ever reaching Connected) is the only state that's
    // eligible for a fresh attempt below.
    if (s == discovery::ConnectionState::Connecting || s == discovery::ConnectionState::Pairing ||
        s == discovery::ConnectionState::Connected) {
      return;
    }
  }

  for (const auto& peer : state.listener->peers()) {
    if (peer.role != discovery::Role::Master) continue;
    if (peer.tcp_port == 0) continue;
    if (!state.known_peers.is_known(peer.device_id)) continue;

    state.tcp_client = std::make_unique<discovery::TcpClient>(
        state.device_id, peer.ip_address, peer.tcp_port, state.known_peers,
        std::chrono::seconds(state.connection_timeout_s));
    state.tcp_client->start();
    return;
  }
}

}  // namespace nockvm::app
