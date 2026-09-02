#include "auto_connect_pump.h"
#include <chrono>
#include "nockvm/discovery/connection_types.h"

namespace nockvm::app {

void pump_auto_connect(AppState& state) {
  if (state.role != discovery::Role::Slave) return;
  if (!state.listener) return;  // discovery hasn't started yet

  // Suppression bookkeeping happens regardless of the auto_connect_enabled
  // toggle, so history stays correct if the user flips it back on later.
  if (state.tcp_client) {
    const discovery::ConnectionInfo info = state.tcp_client->status();
    if (info.state == discovery::ConnectionState::Connected) {
      // A fresh successful connection (however it was started) lifts any
      // earlier suppression for this specific device.
      state.auto_connect_suppressed.erase(info.peer_device_id);
    }
    if (info.go_away_received) {
      state.auto_connect_suppressed.insert(info.peer_device_id);
    }
    if (info.state == discovery::ConnectionState::Connecting || info.state == discovery::ConnectionState::Pairing ||
        info.state == discovery::ConnectionState::Connected) {
      return;  // already busy or connected -- nothing to do
    }
  }

  if (!state.auto_connect_enabled) return;

  for (const auto& peer : state.listener->peers()) {
    if (peer.role != discovery::Role::Master) continue;
    if (peer.tcp_port == 0) continue;
    if (!state.known_peers.is_known(peer.device_id)) continue;
    if (state.auto_connect_suppressed.count(peer.device_id)) continue;

    state.tcp_client = std::make_unique<discovery::TcpClient>(
        state.device_id, peer.ip_address, peer.tcp_port, state.known_peers,
        std::chrono::seconds(state.connection_timeout_s));
    state.tcp_client->start();
    return;
  }
}

}  // namespace nockvm::app
