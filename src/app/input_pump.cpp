#include "input_pump.h"
#include <cstdarg>
#include <cstdio>
#include "nockvm/discovery/connection_types.h"
#include "nockvm/input/inject.h"
#include "nockvm/input/protocol.h"
#include "nockvm/topology/crossing.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace nockvm::app {
namespace {

// TEMPORARY DEBUG LOGGING -- remove once the crossing-coordinate issue is
// diagnosed. Appends to nockvm_debug.log in the process's working directory.
void debug_log(const char* fmt, ...) {
  FILE* f = std::fopen("nockvm_debug.log", "a");
  if (!f) return;
  va_list args;
  va_start(args, fmt);
  std::vfprintf(f, fmt, args);
  va_end(args);
  std::fclose(f);
}

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

void send_modifier_sync_mask(AppState& state, uint8_t mask) {
  const auto payload = input::encode_modifier_sync(mask);
  state.tcp_server->send_input(input::kMsgModifierSync, payload.data(), payload.size());
}

void send_modifier_sync(AppState& state) { send_modifier_sync_mask(state, current_modifier_mask()); }

void deactivate_hook(AppState& state) {
  state.input_hook.resume();
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

  const bool skip_crossing = state.input_just_handed_off;
  state.input_just_handed_off = false;

  const auto arrangement = state.screen_arrangement.get(info.peer_device_id);
  if (bc.crossed) {
    debug_log(
        "[B->A attempt] bc_dir=%d perp=%d skip=%d has_arrangement=%d arr_dir=%d dir_match=%d peer_monitors_empty=%d "
        "logical_before=(%d,%d)\n",
        static_cast<int>(bc.direction), bc.perp_pos, skip_crossing, arrangement.has_value(),
        arrangement ? static_cast<int>(arrangement->direction) : -1, arrangement && arrangement->direction == bc.direction,
        info.peer_monitors.empty(), state.input_logical_x, state.input_logical_y);
  }
  if (!skip_crossing && bc.crossed && arrangement && arrangement->direction == bc.direction &&
      !info.peer_monitors.empty()) {
    const topology::CrossingResult cross =
        topology::compute_crossing(info.peer_monitors, arrangement->direction, arrangement->offset, bc.perp_pos);
    if (cross.has_target) {
      // One frame's delta is batched (everything since the last poll()), so
      // a fast swipe can land well past the boundary before this check even
      // runs. Carry that overshoot into the peer's space along the axis
      // being crossed, instead of discarding it and always landing exactly
      // on the entry edge regardless of how hard the mouse was pushed --
      // real multi-monitor motion doesn't lose momentum at the seam either.
      const bool horizontal = bc.direction == topology::Direction::Left || bc.direction == topology::Direction::Right;
      const int32_t overshoot =
          horizontal ? state.input_logical_x - bc.clamped_x : state.input_logical_y - bc.clamped_y;

      state.input_owned_by_master = false;
      state.input_logical_x = cross.x + (horizontal ? overshoot : 0);
      state.input_logical_y = cross.y + (horizontal ? 0 : overshoot);
      state.input_just_handed_off = true;

      int32_t real_x = 0, real_y = 0;
      input::get_local_cursor_pos(real_x, real_y);
      debug_log("[B->A] dir=%d perp=%d clamped=(%d,%d) overshoot=%d cross_raw=(%d,%d) sent=(%d,%d) real_master_cursor=(%d,%d)\n",
                static_cast<int>(bc.direction), bc.perp_pos, bc.clamped_x, bc.clamped_y, overshoot, cross.x, cross.y,
                state.input_logical_x, state.input_logical_y, real_x, real_y);

      state.input_hook.suppress();
      const auto payload = input::encode_mouse_absolute(state.input_logical_x, state.input_logical_y);
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

  const bool skip_crossing = state.input_just_handed_off;
  state.input_just_handed_off = false;

  // The landing point on entry sits exactly on this same edge, so touch
  // alone (overshoot == 0) would flag every frame at rest right at the
  // boundary as a crossing. Require a deliberate push past it, not just
  // reaching it, before honoring a crossing back the other way.
  constexpr int32_t kReturnMargin = 4;
  const int32_t overshoot_x = state.input_logical_x - bc.clamped_x;
  const int32_t overshoot_y = state.input_logical_y - bc.clamped_y;
  const bool past_margin =
      bc.crossed && ((overshoot_x >= kReturnMargin || overshoot_x <= -kReturnMargin) ||
                      (overshoot_y >= kReturnMargin || overshoot_y <= -kReturnMargin));

  if (bc.crossed) {
    debug_log("[A->B attempt] bc_dir=%d perp=%d skip=%d overshoot=(%d,%d) past_margin=%d logical_before=(%d,%d)\n",
              static_cast<int>(bc.direction), bc.perp_pos, skip_crossing, overshoot_x, overshoot_y, past_margin,
              state.input_logical_x, state.input_logical_y);
  }

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

  if (skip_crossing || !past_margin) return;
  const auto arrangement = state.screen_arrangement.get(info.peer_device_id);
  if (!arrangement) {
    debug_log("[A->B attempt] rejected: no arrangement for this peer\n");
    return;
  }

  const topology::ClusterBounds master_bounds = topology::compute_bounds(state.local_monitors);
  const topology::ArrangementEntry inv = topology::invert_entry(*arrangement, master_bounds, peer_bounds);
  if (bc.direction != inv.direction) {
    debug_log("[A->B attempt] rejected: bc_dir=%d != inv_dir=%d (arrangement dir=%d)\n", static_cast<int>(bc.direction),
              static_cast<int>(inv.direction), static_cast<int>(arrangement->direction));
    return;
  }

  const topology::CrossingResult cross =
      topology::compute_crossing(state.local_monitors, inv.direction, inv.offset, bc.perp_pos);
  if (!cross.has_target) {
    debug_log("[A->B attempt] rejected: compute_crossing has_target=false\n");
    return;
  }

  // Carry the overshoot already computed above (the margin check) into
  // Master's space along the axis being crossed, for the same reason as the
  // outbound crossing in handle_master_owned -- don't discard real momentum.
  const bool horizontal = bc.direction == topology::Direction::Left || bc.direction == topology::Direction::Right;
  const int32_t overshoot = horizontal ? overshoot_x : overshoot_y;

  state.input_owned_by_master = true;
  state.input_logical_x = cross.x + (horizontal ? overshoot : 0);
  state.input_logical_y = cross.y + (horizontal ? 0 : overshoot);
  state.input_just_handed_off = true;
  state.input_hook.resume(state.input_logical_x, state.input_logical_y);

  int32_t real_x = 0, real_y = 0;
  input::get_local_cursor_pos(real_x, real_y);
  debug_log("[A->B] dir=%d perp=%d clamped=(%d,%d) overshoot=%d cross_raw=(%d,%d) sent=(%d,%d) real_master_cursor_after_resume=(%d,%d)\n",
            static_cast<int>(bc.direction), bc.perp_pos, bc.clamped_x, bc.clamped_y, overshoot, cross.x, cross.y,
            state.input_logical_x, state.input_logical_y, real_x, real_y);

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
    state.input_hook.resume();
    state.input_owned_by_master = true;
    // resume() with no target leaves the real cursor (and the hook's own
    // anchor) wherever suppression parked it -- sync logical tracking to
    // that same real position, or it would keep accumulating from a stale
    // Slave-space coordinate and corrupt every crossing check after this.
    state.input_logical_x = state.input_hook.anchor_x_;
    state.input_logical_y = state.input_hook.anchor_y_;
    // Force-clear rather than reading current physical state: the user is
    // still physically holding the hotkey's own modifiers right now, and
    // will release them after control has already returned to Master (so
    // those releases never reach Slave) -- reading "currently held" here
    // would leave Slave's modifiers stuck down instead of freed.
    send_modifier_sync_mask(state, 0);
    return;
  }

  if (state.input_owned_by_master) {
    handle_master_owned(state, info, frame);
  } else {
    handle_slave_owned(state, info, frame);
  }
}

}  // namespace nockvm::app
