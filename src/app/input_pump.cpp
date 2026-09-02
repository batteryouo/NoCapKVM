#include "input_pump.h"
#include "nockvm/discovery/connection_types.h"
#include "nockvm/input/inject.h"
#include "nockvm/input/protocol.h"
#include "nockvm/topology/crossing.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace nockvm::app {
namespace {

uint8_t current_modifier_mask() {
#ifdef _WIN32
  uint8_t mask = 0;
  if (GetAsyncKeyState(VK_SHIFT) & 0x8000) mask |= 1;
  if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mask |= 2;
  if (GetAsyncKeyState(VK_MENU) & 0x8000) mask |= 4;
  if (GetAsyncKeyState(VK_LWIN) & 0x8000) mask |= 8;
  return mask;
#else
  return 0;
#endif
}

void send_modifier_sync(AppState& state) {
  const auto payload = input::encode_modifier_sync(current_modifier_mask());
  state.tcp_server->send_input(input::kMsgModifierSync, payload.data(), payload.size());
}

void deactivate_hook(AppState& state) {
  state.input_hook.set_suppress(false);
  state.input_hook.uninstall();
  state.input_hook_active = false;
  state.input_owned_by_master = true;
}

void handle_master_owned(AppState& state, const discovery::ConnectionInfo& info, const input::InputFrame& frame) {
  state.input_logical_x += frame.dx;
  state.input_logical_y += frame.dy;

  const topology::ClusterBounds master_bounds = topology::compute_bounds(state.local_monitors);
  const topology::BoundaryCheck bc =
      topology::check_boundary(master_bounds, state.input_logical_x, state.input_logical_y);

  const auto arrangement = state.screen_arrangement.get(info.peer_device_id);
  if (bc.crossed && arrangement && arrangement->direction == bc.direction && !info.peer_monitors.empty()) {
    const topology::CrossingResult cross =
        topology::compute_crossing(info.peer_monitors, arrangement->direction, arrangement->offset, bc.perp_pos);
    if (cross.has_target) {
      state.input_owned_by_master = false;
      state.input_logical_x = cross.x;
      state.input_logical_y = cross.y;
      state.input_hook.set_suppress(true);
      const auto payload = input::encode_mouse_absolute(cross.x, cross.y);
      state.tcp_server->send_input(input::kMsgMouseAbsolute, payload.data(), payload.size());
      send_modifier_sync(state);
      return;
    }
  }

  state.input_logical_x = bc.clamped_x;
  state.input_logical_y = bc.clamped_y;
  // Buttons/wheel/keys already reached the local OS untouched (suppress == false).
}

void handle_slave_owned(AppState& state, const discovery::ConnectionInfo& info, const input::InputFrame& frame) {
  state.input_logical_x += frame.dx;
  state.input_logical_y += frame.dy;

  const topology::ClusterBounds peer_bounds = topology::compute_bounds(info.peer_monitors);
  const topology::BoundaryCheck bc =
      topology::check_boundary(peer_bounds, state.input_logical_x, state.input_logical_y);
  state.input_logical_x = bc.clamped_x;
  state.input_logical_y = bc.clamped_y;

  {
    const auto payload = input::encode_mouse_absolute(state.input_logical_x, state.input_logical_y);
    state.tcp_server->send_input(input::kMsgMouseAbsolute, payload.data(), payload.size());
  }
  for (const auto& b : frame.buttons) {
    const auto payload = input::encode_mouse_button(b.button, b.down);
    state.tcp_server->send_input(input::kMsgMouseButton, payload.data(), payload.size());
  }
  for (const int16_t delta : frame.wheel_deltas) {
    const auto payload = input::encode_mouse_wheel(delta);
    state.tcp_server->send_input(input::kMsgMouseWheel, payload.data(), payload.size());
  }
  for (const auto& k : frame.keys) {
    const auto payload = input::encode_key(k.vk, k.scancode, k.down, k.extended);
    state.tcp_server->send_input(input::kMsgKey, payload.data(), payload.size());
  }

  if (!bc.crossed) return;
  const auto arrangement = state.screen_arrangement.get(info.peer_device_id);
  if (!arrangement) return;

  const topology::ClusterBounds master_bounds = topology::compute_bounds(state.local_monitors);
  const topology::ArrangementEntry inv = topology::invert_entry(*arrangement, master_bounds, peer_bounds);
  if (bc.direction != inv.direction) return;

  const topology::CrossingResult cross =
      topology::compute_crossing(state.local_monitors, inv.direction, inv.offset, bc.perp_pos);
  if (!cross.has_target) return;

  state.input_owned_by_master = true;
  state.input_logical_x = cross.x;
  state.input_logical_y = cross.y;
  input::set_local_cursor_pos(cross.x, cross.y);
  state.input_hook.set_suppress(false);
  send_modifier_sync(state);
}

}  // namespace

void pump_input(AppState& state) {
  if (state.role != discovery::Role::Master) return;

  // tcp_server can be reset out from under an active hook (e.g. the
  // Discovery screen's "Back" button) — deactivate unconditionally here
  // rather than skipping past this whole function, or a Slave-owned
  // suppression could be left stuck on with no send_input left to route an
  // escape-hotkey response through.
  if (!state.tcp_server) {
    if (state.input_hook_active) deactivate_hook(state);
    return;
  }

  const discovery::ConnectionInfo info = state.tcp_server->status();
  const bool connected = info.state == discovery::ConnectionState::Connected;

  if (!connected) {
    if (state.input_hook_active) deactivate_hook(state);
    return;
  }

  if (!state.input_hook_active) {
    state.input_hook.install();
    state.input_hook_active = true;
    state.input_owned_by_master = true;
    // Seed from the real cursor position (matching InputHook's own internal
    // anchor) rather than e.g. the screen center — otherwise logical
    // tracking starts offset from reality until the mouse happens to visit
    // wherever was guessed, throwing off the first crossing check.
    if (!input::get_local_cursor_pos(state.input_logical_x, state.input_logical_y)) {
      const topology::ClusterBounds b = topology::compute_bounds(state.local_monitors);
      state.input_logical_x = (b.min_x + b.max_x) / 2;
      state.input_logical_y = (b.min_y + b.max_y) / 2;
    }
  }

  const input::InputFrame frame = state.input_hook.poll();

  if (frame.escape_pressed && !state.input_owned_by_master) {
    state.input_hook.set_suppress(false);
    state.input_owned_by_master = true;
    send_modifier_sync(state);
    return;
  }

  if (state.input_owned_by_master) {
    handle_master_owned(state, info, frame);
  } else {
    handle_slave_owned(state, info, frame);
  }
}

}  // namespace nockvm::app
