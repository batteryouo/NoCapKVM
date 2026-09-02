#include "input_pump.h"
#include <algorithm>
#include "nockvm/discovery/connection_types.h"
#include "nockvm/input/inject.h"
#include "nockvm/input/protocol.h"
#include "nockvm/topology/crossing.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace nockvm::app {
namespace {

#ifdef _WIN32
// Level-triggered rather than edge-triggered on Escape's own keydown: the
// original check only looked at whether Ctrl/Alt/Shift were held at the
// exact instant Escape's keydown fired, so if Escape happened to be pressed
// slightly before the modifiers were fully seated, that one check silently
// failed and nothing re-armed it without releasing and re-pressing Escape.
// Checking the live held-keys set every frame instead means it fires as
// soon as all four are simultaneously down, regardless of press order.
bool escape_combo_held(const AppState& state) {
  bool ctrl = false, alt = false, shift = false, esc = false;
  for (const uint32_t vk : state.input_held_vks) {
    switch (vk) {
      case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: ctrl = true; break;
      case VK_MENU: case VK_LMENU: case VK_RMENU: alt = true; break;
      case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: shift = true; break;
      case VK_ESCAPE: esc = true; break;
      default: break;
    }
  }
  return ctrl && alt && shift && esc;
}
#else
bool escape_combo_held(const AppState&) { return false; }
#endif

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
  state.input_held_vks.clear();
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

  // Send the CLAMPED position (the visible cursor on Slave should stay
  // pinned at its own screen edge until a crossing actually commits) but do
  // NOT write it back into state.input_logical_x/y here: doing that
  // unconditionally every frame was the actual bug behind "almost always
  // fails" -- it discarded any overshoot that didn't clear the margin
  // within a single frame's batched delta, so a gentle sustained push
  // against the edge (completely normal) never accumulated across frames;
  // each frame started over exactly at the boundary. Below, only a
  // successful crossing reassigns state.input_logical_x/y.
  {
    const auto payload = input::encode_mouse_absolute(bc.clamped_x, bc.clamped_y);
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
  if (!arrangement) return;

  const topology::ClusterBounds master_bounds = topology::compute_bounds(state.local_monitors);
  const topology::ArrangementEntry inv = topology::invert_entry(*arrangement, master_bounds, peer_bounds);
  if (bc.direction != inv.direction) return;

  const topology::CrossingResult cross =
      topology::compute_crossing(state.local_monitors, inv.direction, inv.offset, bc.perp_pos);
  if (!cross.has_target) return;

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

  // On-screen key monitor (diagnostic): tracks which virtual-key codes are
  // currently held, independent of ownership, so hotkey detection issues
  // can be observed directly instead of guessed at.
  for (const auto& k : frame.keys) {
    auto& held = state.input_held_vks;
    const auto it = std::find(held.begin(), held.end(), k.vk);
    if (k.down) {
      if (it == held.end()) held.push_back(k.vk);
    } else if (it != held.end()) {
      held.erase(it);
    }
  }

  if (escape_combo_held(state) && !state.input_owned_by_master) {
    // Land at the center of Master's own bounds, not wherever the real
    // cursor happens to be sitting: since suppress() stopped moving the
    // cursor at all, the real position (and the hook's anchor) is still
    // exactly the edge that was originally crossed -- resuming there with
    // no margin meant the very next frame immediately re-detected that
    // same edge and crossed straight back to Slave (the reported
    // back-and-forth toggle). An emergency reset can afford a visible
    // jump; landing solidly in the interior is what actually matters.
    const topology::ClusterBounds b = topology::compute_bounds(state.local_monitors);
    const int32_t safe_x = (b.min_x + b.max_x) / 2;
    const int32_t safe_y = (b.min_y + b.max_y) / 2;
    state.input_hook.resume(safe_x, safe_y);
    state.input_owned_by_master = true;
    state.input_logical_x = safe_x;
    state.input_logical_y = safe_y;
    state.input_just_handed_off = true;
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
