#pragma once
#include <cstdint>

namespace nockvm::input {

// All of the following are Windows-only (SendInput-based); on non-Windows
// builds they are inert no-ops.

// Injects an absolute cursor position, in this machine's own pixel/virtual-
// desktop coordinate space (the same space MonitorInfo uses).
void inject_mouse_absolute(int32_t x, int32_t y);
void inject_mouse_button(uint8_t button, bool down);
void inject_mouse_wheel(int16_t delta);
void inject_key(uint32_t vk, uint32_t scancode, bool down, bool extended);

// Forces each of Shift/Ctrl/Alt/Meta to the held (1) or released (0) state
// given by the low 4 bits of mask, regardless of their current state.
// Called on ownership handoff so a missed keydown/keyup elsewhere can't
// leave a modifier stuck held on this machine.
void set_modifiers(uint8_t mask);

// Reads this machine's own real cursor position (plain GetCursorPos) — used
// to seed logical position tracking with the same value InputHook's
// anchor starts from, on non-Windows always returns false.
bool get_local_cursor_pos(int32_t& x, int32_t& y);

// Configures the coordinate range inject_mouse_absolute() maps into (this
// machine's own virtual-desktop bounds). Windows ignores this (SendInput's
// own GetSystemMetrics-based normalization is used instead); Linux needs it
// to size its virtual absolute-pointer device before first use. Call once
// at startup, before any connection (and so any inject_* call) can happen.
void configure_pointer_bounds(int32_t min_x, int32_t max_x, int32_t min_y, int32_t max_y);

}  // namespace nockvm::input
