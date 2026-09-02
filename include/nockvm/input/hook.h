#pragma once
#include <atomic>
#include <cstdint>
#include <vector>

namespace nockvm::input {

struct MouseButtonEvent {
  uint8_t button;  // 0=Left, 1=Right, 2=Middle, 3=X1, 4=X2
  bool down;
};

struct KeyEvent {
  uint32_t vk;
  uint32_t scancode;
  bool down;
  bool extended;
};

struct InputFrame {
  int32_t dx = 0, dy = 0;  // accumulated raw mouse delta since the last poll()
  std::vector<MouseButtonEvent> buttons;
  std::vector<int16_t> wheel_deltas;
  std::vector<KeyEvent> keys;
  bool escape_pressed = false;  // Ctrl+Alt+Shift+Esc observed since last poll()
};

// Windows-only global mouse/keyboard capture via WH_MOUSE_LL/WH_KEYBOARD_LL.
// install() must be called from a thread with an active Windows message
// pump (the main thread's glfwPollEvents() already provides this every
// frame), since low-level hook callbacks run on that thread. poll() must be
// called from that same thread, once per frame, right after
// glfwPollEvents() — the hook callbacks and poll() are then strictly
// sequential on one thread, so no internal locking is needed.
//
// On non-Windows builds this is an inert stub: install() returns false,
// poll() always returns an empty frame.
class InputHook {
public:
  InputHook();
  ~InputHook();
  InputHook(const InputHook&) = delete;
  InputHook& operator=(const InputHook&) = delete;

  bool install();
  void uninstall();

  // Starts suppressing mouse-move/button/wheel/key events from reaching the
  // local OS. Warps the real cursor to a fixed point away from any screen
  // edge first (not wherever it currently is — it's typically sitting
  // exactly at the edge that was just crossed, and the OS clamps cursor
  // position to the desktop bounds regardless of suppression, so an anchor
  // left at the edge would clamp future pushes back to zero delta just like
  // the unsuppressed case does). Each subsequent blocked move re-centers the
  // real cursor back to that anchor so raw deltas keep flowing indefinitely
  // (the same technique Synergy/Barrier use, since a low-level hook cannot
  // rewrite MSLLHOOKSTRUCT::pt and pass it through).
  void suppress();

  // Stops suppressing; events pass through to the local OS untouched again.
  void resume();

  // Same as resume(), but also warps the real cursor to (x, y) first and
  // syncs the anchor to match — use this when handing control back to a
  // specific landing point, so the next real move's delta isn't a spurious
  // jump from the stale suppression anchor.
  void resume(int32_t x, int32_t y);

  // Drains everything accumulated since the last call.
  InputFrame poll();

  // Internal state written by the platform hook callbacks in hook.cpp
  // (which, being plain WH_MOUSE_LL/WH_KEYBOARD_LL callbacks, cannot be
  // member functions and so need public access here instead of friendship
  // requiring platform types in this header). Not for application use.
  std::atomic<bool> suppress_{false};
  int32_t anchor_x_ = 0, anchor_y_ = 0;  // real cursor position pinned to while suppress_ is true
  InputFrame pending_;                  // accumulates between poll() calls; both only ever touched
                                         // from the thread that owns the Windows message pump

private:
  void* mouse_hook_ = nullptr;
  void* keyboard_hook_ = nullptr;
};

}  // namespace nockvm::input
