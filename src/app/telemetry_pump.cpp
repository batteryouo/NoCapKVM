#include "telemetry_pump.h"
#include "nockvm/discovery/connection_types.h"
#include "nockvm/telemetry/telemetry.h"
#include "reconnect_tracker.h"

namespace nockvm::app {
namespace {

const char* state_name(discovery::ConnectionState s) {
  switch (s) {
    case discovery::ConnectionState::Idle: return "idle";
    case discovery::ConnectionState::Connecting: return "connecting";
    case discovery::ConnectionState::Pairing: return "pairing";
    case discovery::ConnectionState::Connected: return "connected";
    case discovery::ConnectionState::Failed: return "failed";
  }
  return "unknown";
}

}  // namespace

void pump_telemetry(AppState& state, double frame_time_ms, bool window_visible) {
  if (state.telemetry_last_window_visible != window_visible) {
    telemetry::log_event(window_visible ? "window.shown" : "window.hidden",
                          window_visible ? "window restored from tray" : "window hidden to tray");
    state.telemetry_last_window_visible = window_visible;
  }

  discovery::ConnectionState conn_state = discovery::ConnectionState::Idle;
  if (state.role == discovery::Role::Master) {
    if (state.tcp_server) conn_state = state.tcp_server->status().state;
  } else if (state.tcp_client) {
    conn_state = state.tcp_client->status().state;
  }

  track_reconnect(state.telemetry_last_connection_state == discovery::ConnectionState::Connected,
                   conn_state == discovery::ConnectionState::Connected, state.ever_connected, state.reconnect_count);

  if (conn_state != state.telemetry_last_connection_state) {
    if (conn_state == discovery::ConnectionState::Connected) {
      telemetry::log_event("connection.established", state_name(conn_state));
    } else if (state.telemetry_last_connection_state == discovery::ConnectionState::Connected) {
      telemetry::log_event("connection.dropped", state_name(conn_state));
    } else {
      telemetry::log_event("connection.state_changed", state_name(conn_state));
    }
    state.telemetry_last_connection_state = conn_state;
  }

  telemetry::FrameContext ctx;
  ctx.window_visible = window_visible;
  ctx.connected = conn_state == discovery::ConnectionState::Connected;
  ctx.role = state.role == discovery::Role::Master ? "master" : "slave";
  ctx.connection_state = state_name(conn_state);
  if (state.role == discovery::Role::Master && state.audio_playback) {
    ctx.audio_buffer_depth = state.audio_playback->buffered_packets();
    ctx.audio_buffer_cap = state.audio_playback->buffer_capacity();
    ctx.audio_jitter_cap_episodes = state.audio_playback->buffer_cap_episodes();
    ctx.audio_playback_generation = state.audio_playback_generation;
  }

  telemetry::tick(frame_time_ms, ctx);
}

}  // namespace nockvm::app
