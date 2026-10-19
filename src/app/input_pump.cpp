#include "input_pump.h"
#include <algorithm>
#include <optional>
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
bool escape_combo_held(const InputSession& s) {
  bool ctrl = false, alt = false, shift = false, esc = false;
  for (const HeldKey& k : s.held_keys) {
    switch (k.vk) {
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
bool escape_combo_held(const InputSession&) { return false; }
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

void send_modifier_sync_mask(InputSession& s, uint8_t mask) {
  const auto payload = input::encode_modifier_sync(mask);
  s.server.send_input(input::kMsgModifierSync, payload.data(), payload.size());
}

void send_modifier_sync(InputSession& s) { send_modifier_sync_mask(s, current_modifier_mask()); }

// Whichever side is losing control at a handoff will never see the keyup
// for anything held right now: while Slave owns input the hook swallows
// every keyup before Windows can see it, and while Master owns it none are
// forwarded to Slave. So each held key's press and its release land on
// opposite machines, and the one that got only the press is stuck holding
// it -- observed as Slave autorepeating the last letter typed
// ("ccccccc...") and as Windows acting like Ctrl were still down (wheel
// scrolling zooms a browser instead of scrolling it). Both handoffs
// therefore release explicitly, each toward the side it's leaving.

void release_held_keys_on_slave(InputSession& s) {
  for (const HeldKey& k : s.held_keys) {
    const auto payload = input::encode_key(k.vk, k.scancode, false, k.extended);
    s.server.send_input(input::kMsgKey, payload.data(), payload.size());
  }
  // The injector tracks modifiers in its own shadow state alongside plain
  // key events (uinput is write-only, so it has nothing to read back), and
  // that shadow is what set_modifiers() diffs against. Clear it too, so a
  // modifier can't survive as "held" there after its key event released it.
  send_modifier_sync_mask(s, 0);
}

#ifdef _WIN32
void release_held_keys_locally(const InputSession& s) {
  // SendInput-synthesized, so both hook procs skip these (LLKHF_INJECTED)
  // and they're never forwarded on to Slave -- correctly, since the key is
  // genuinely still down physically and its real keyup belongs to Slave
  // now. This only corrects Windows' own idea of what's held.
  for (const HeldKey& k : s.held_keys) input::inject_key(k.vk, k.scancode, false, k.extended);
}
#else
void release_held_keys_locally(const InputSession&) {}
#endif

void deactivate_hook(InputSession& s) {
  s.hook.resume();
  s.hook.uninstall();
  s.hook_active = false;
  s.owned_by_master = true;
  s.held_keys.clear();
}

void handle_master_owned(InputSession& s, const discovery::ConnectionInfo& info, const input::InputFrame& frame) {
  s.logical_x += frame.dx;
  s.logical_y += frame.dy;

  const topology::ClusterBounds master_bounds = topology::compute_bounds(s.local_monitors);
  const topology::BoundaryCheck bc =
      topology::check_boundary(master_bounds, s.logical_x, s.logical_y);

  const bool skip_crossing = s.just_handed_off;
  s.just_handed_off = false;

  const auto arrangement = s.arrangement.get(info.peer_device_id);
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
          horizontal ? s.logical_x - bc.clamped_x : s.logical_y - bc.clamped_y;

      s.owned_by_master = false;
      s.logical_x = cross.x + (horizontal ? overshoot : 0);
      s.logical_y = cross.y + (horizontal ? 0 : overshoot);
      s.just_handed_off = true;
      s.hook.suppress();

      const auto payload = input::encode_mouse_absolute(s.logical_x, s.logical_y);
      s.server.send_input_latest(input::kMsgMouseAbsolute, payload.data(), payload.size());
      // Order matters: current_modifier_mask() reads the real physical
      // state, so it has to run before the local release below fakes those
      // keys up. Slave wants what's actually held; Windows must forget it.
      send_modifier_sync(s);
      release_held_keys_locally(s);
      return;
    }
  }

  s.logical_x = bc.clamped_x;
  s.logical_y = bc.clamped_y;
  // Buttons/wheel/keys already reached the local OS untouched (suppress == false).
}

void handle_slave_owned(InputSession& s, const discovery::ConnectionInfo& info, const input::InputFrame& frame) {
  s.logical_x += frame.dx;
  s.logical_y += frame.dy;

  const topology::ClusterBounds peer_bounds = topology::compute_bounds(info.peer_monitors);
  const topology::BoundaryCheck bc =
      topology::check_boundary(peer_bounds, s.logical_x, s.logical_y);

  const bool skip_crossing = s.just_handed_off;
  s.just_handed_off = false;

  // The landing point on entry sits exactly on this same edge, so touch
  // alone (overshoot == 0) would flag every frame at rest right at the
  // boundary as a crossing. Require a deliberate push past it, not just
  // reaching it, before honoring a crossing back the other way.
  constexpr int32_t kReturnMargin = 4;
  const int32_t overshoot_x = s.logical_x - bc.clamped_x;
  const int32_t overshoot_y = s.logical_y - bc.clamped_y;
  const bool past_margin =
      bc.crossed && ((overshoot_x >= kReturnMargin || overshoot_x <= -kReturnMargin) ||
                      (overshoot_y >= kReturnMargin || overshoot_y <= -kReturnMargin));

  // Exactly one of the peer's four edges faces Master, and it's the only
  // one where pushing past can lead anywhere. Working that out here rather
  // than only at the crossing check below is what keeps the other three
  // edges from accumulating overshoot forever (see the clamp further down).
  const auto arrangement = s.arrangement.get(info.peer_device_id);
  const topology::ClusterBounds master_bounds = topology::compute_bounds(s.local_monitors);
  std::optional<topology::ArrangementEntry> inv;
  if (arrangement) inv = topology::invert_entry(*arrangement, master_bounds, peer_bounds);
  const bool crossable_edge = bc.crossed && inv && bc.direction == inv->direction;
  const bool horizontal = bc.direction == topology::Direction::Left || bc.direction == topology::Direction::Right;

  // Send the CLAMPED position -- the visible cursor on Slave should stay
  // pinned at its own screen edge until a crossing actually commits. What
  // s.logical_x/y itself becomes is decided further down, after
  // the overshoots above have been read: writing the clamp back here
  // unconditionally was the original bug behind "crossing back almost
  // always fails", since it discarded any overshoot that didn't clear the
  // margin within one frame's batched delta.
  {
    const auto payload = input::encode_mouse_absolute(bc.clamped_x, bc.clamped_y);
    s.server.send_input_latest(input::kMsgMouseAbsolute, payload.data(), payload.size());
  }
  for (const auto& b : frame.buttons) {
    const auto payload = input::encode_mouse_button(b.button, b.down);
    s.server.send_input(input::kMsgMouseButton, payload.data(), payload.size());
  }
  for (const int16_t delta : frame.wheel_deltas) {
    const auto payload = input::encode_mouse_wheel(delta);
    s.server.send_input(input::kMsgMouseWheel, payload.data(), payload.size());
  }
  for (const auto& k : frame.keys) {
    const auto payload = input::encode_key(k.vk, k.scancode, k.down, k.extended);
    s.server.send_input(input::kMsgKey, payload.data(), payload.size());
  }

  // Preserve overshoot only on an edge that can return control to Master.
  if (crossable_edge) {
    if (horizontal) s.logical_y = bc.clamped_y;
    else s.logical_x = bc.clamped_x;
  } else {
    s.logical_x = bc.clamped_x;
    s.logical_y = bc.clamped_y;
  }

  if (skip_crossing || !past_margin || !crossable_edge) return;

  const topology::CrossingResult cross =
      topology::compute_crossing(s.local_monitors, inv->direction, inv->offset, bc.perp_pos);
  if (!cross.has_target) return;

  // Carry the overshoot already computed above (the margin check) into
  // Master's space along the axis being crossed, for the same reason as the
  // outbound crossing in handle_master_owned -- don't discard real momentum.
  const int32_t overshoot = horizontal ? overshoot_x : overshoot_y;

  s.owned_by_master = true;
  s.logical_x = cross.x + (horizontal ? overshoot : 0);
  s.logical_y = cross.y + (horizontal ? 0 : overshoot);
  s.just_handed_off = true;
  // Release remote keys before input stops reaching Slave.
  release_held_keys_on_slave(s);
  s.hook.resume(s.logical_x, s.logical_y);
}

}  // namespace

void deactivate_input(InputSession& s) {
  if (s.hook_active) deactivate_hook(s);
}

InputStatus input_status(const InputSession& s) { return InputStatus{s.owned_by_master, s.held_keys}; }

void pump_input(InputSession& s) {
  const discovery::ConnectionInfo info = s.server.status();
  const bool connected = info.state == discovery::ConnectionState::Connected;

  if (!connected) {
    if (s.hook_active) deactivate_hook(s);
    return;
  }

  if (!s.hook_active) {
    s.hook.install();
    s.hook_active = true;
    s.owned_by_master = true;
    // Start logical tracking from the cursor's actual position.
    if (!input::get_local_cursor_pos(s.logical_x, s.logical_y)) {
      const topology::ClusterBounds b = topology::compute_bounds(s.local_monitors);
      s.logical_x = (b.min_x + b.max_x) / 2;
      s.logical_y = (b.min_y + b.max_y) / 2;
    }
  }

  // A Slave can lose its last display while it owns input. There is then no
  // remote desktop boundary from which a normal return crossing could be
  // detected, so release the local hook immediately instead of leaving all
  // physical mouse movement suppressed.
  if (!s.owned_by_master && info.peer_monitors.empty()) {
    const topology::ClusterBounds b = topology::compute_bounds(s.local_monitors);
    const int32_t safe_x = (b.min_x + b.max_x) / 2;
    const int32_t safe_y = (b.min_y + b.max_y) / 2;
    s.hook.resume(safe_x, safe_y);
    s.owned_by_master = true;
    s.logical_x = safe_x;
    s.logical_y = safe_y;
    s.just_handed_off = true;
    release_held_keys_on_slave(s);
    return;
  }

  const input::InputFrame frame = s.hook.poll();

  // Track held keys so handoffs can release them on the previous owner.
  for (const auto& k : frame.keys) {
    auto& held = s.held_keys;
    const auto it = std::find_if(held.begin(), held.end(), [&](const HeldKey& h) { return h.vk == k.vk; });
    if (k.down) {
      // Windows repeats keydowns while a key is held; refresh rather than
      // duplicate, so an autorepeat can't stack entries or leave a stale
      // scancode behind for the release below to use.
      if (it == held.end()) held.push_back(HeldKey{k.vk, k.scancode, k.extended});
      else *it = HeldKey{k.vk, k.scancode, k.extended};
    } else if (it != held.end()) {
      held.erase(it);
    }
  }

  if (escape_combo_held(s) && !s.owned_by_master) {
    // Resume in the interior to prevent an immediate boundary crossing.
    const topology::ClusterBounds b = topology::compute_bounds(s.local_monitors);
    const int32_t safe_x = (b.min_x + b.max_x) / 2;
    const int32_t safe_y = (b.min_y + b.max_y) / 2;
    s.hook.resume(safe_x, safe_y);
    s.owned_by_master = true;
    s.logical_x = safe_x;
    s.logical_y = safe_y;
    s.just_handed_off = true;
    // Force-release rather than syncing current physical state: the user is
    // still physically holding the hotkey itself right now, and will let go
    // of it after control has already returned to Master (so those releases
    // never reach Slave) -- sending "currently held" here would leave every
    // one of those keys stuck down on Slave instead of freed. Same reason
    // the ordinary crossing back does this; the only difference is that
    // here the keys involved are the hotkey's own.
    release_held_keys_on_slave(s);
    // The emergency escape is a hard reset, not just a handoff: drop the
    // connection outright rather than leaving it up. Reconnecting is
    // currently manual (Slave has to click Connect again) -- a known,
    // accepted rough edge for now rather than something to solve here.
    s.server.disconnect_current();
    return;
  }

  if (s.owned_by_master) {
    handle_master_owned(s, info, frame);
  } else {
    handle_slave_owned(s, info, frame);
  }
}

}  // namespace nockvm::app
