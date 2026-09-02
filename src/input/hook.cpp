#include "nockvm/input/hook.h"

#ifdef _WIN32
#include <windows.h>

namespace nockvm::input {
namespace {

InputHook* g_instance = nullptr;

LRESULT CALLBACK mouse_proc(int code, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam);

}  // namespace

InputHook::InputHook() = default;

InputHook::~InputHook() { uninstall(); }

bool InputHook::install() {
  if (g_instance) return false;
  g_instance = this;

  POINT pt;
  GetCursorPos(&pt);
  anchor_x_ = pt.x;
  anchor_y_ = pt.y;

  mouse_hook_ = SetWindowsHookExW(WH_MOUSE_LL, mouse_proc, nullptr, 0);
  keyboard_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_proc, nullptr, 0);
  if (!mouse_hook_ || !keyboard_hook_) {
    uninstall();
    return false;
  }
  return true;
}

void InputHook::uninstall() {
  if (mouse_hook_) {
    UnhookWindowsHookEx(static_cast<HHOOK>(mouse_hook_));
    mouse_hook_ = nullptr;
  }
  if (keyboard_hook_) {
    UnhookWindowsHookEx(static_cast<HHOOK>(keyboard_hook_));
    keyboard_hook_ = nullptr;
  }
  if (g_instance == this) g_instance = nullptr;
}

void InputHook::suppress() {
  // Center of the whole virtual desktop (spanning all monitors) — guaranteed
  // interior, not at any physical edge, regardless of monitor layout.
  const int32_t x = GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) / 2;
  const int32_t y = GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN) / 2;
  // Update anchor/suppress_ BEFORE the SetCursorPos call, not after: if that
  // call synchronously re-enters mouse_proc (observed intermittently), the
  // hook must already see the final state, or its own suppressed-branch
  // recenter logic reads the stale (pre-update) anchor and stomps this
  // target position right back to wherever suppression last centered.
  anchor_x_ = x;
  anchor_y_ = y;
  suppress_.store(true);
  SetCursorPos(x, y);
}

void InputHook::resume() { suppress_.store(false); }

void InputHook::resume(int32_t x, int32_t y) {
  anchor_x_ = x;
  anchor_y_ = y;
  suppress_.store(false);
  SetCursorPos(x, y);
}

InputFrame InputHook::poll() {
  InputFrame frame = std::move(pending_);
  pending_ = InputFrame{};
  return frame;
}

namespace {

uint8_t button_from_wparam(WPARAM wparam, LPARAM mouse_data_high_word) {
  switch (wparam) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP: return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP: return 1;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP: return 2;
    default: return mouse_data_high_word == XBUTTON1 ? 3 : 4;  // WM_XBUTTONDOWN/UP
  }
}

LRESULT CALLBACK mouse_proc(int code, WPARAM wparam, LPARAM lparam) {
  InputHook* self = g_instance;
  if (code < 0 || !self) return CallNextHookEx(nullptr, code, wparam, lparam);

  auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lparam);
  // Ignore SendInput-synthesized events: without this, testing Master and
  // Slave as two processes on one machine would have Master's hook fight
  // its own Slave process's injected moves/clicks (both share one real
  // desktop/cursor locally, unlike the real two-machine deployment where
  // each side's input is naturally isolated to its own desktop).
  if (info->flags & LLMHF_INJECTED) return CallNextHookEx(nullptr, code, wparam, lparam);

  const bool suppress = self->suppress_.load();

  if (wparam == WM_MOUSEMOVE) {
    self->pending_.dx += info->pt.x - self->anchor_x_;
    self->pending_.dy += info->pt.y - self->anchor_y_;
    if (suppress) {
      SetCursorPos(self->anchor_x_, self->anchor_y_);
      return 1;
    }
    self->anchor_x_ = info->pt.x;
    self->anchor_y_ = info->pt.y;
    return CallNextHookEx(nullptr, code, wparam, lparam);
  }

  if (wparam == WM_MOUSEWHEEL) {
    self->pending_.wheel_deltas.push_back(static_cast<int16_t>(HIWORD(info->mouseData)));
    return suppress ? 1 : CallNextHookEx(nullptr, code, wparam, lparam);
  }

  switch (wparam) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
      self->pending_.buttons.push_back({button_from_wparam(wparam, HIWORD(info->mouseData)), true});
      return suppress ? 1 : CallNextHookEx(nullptr, code, wparam, lparam);
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
      self->pending_.buttons.push_back({button_from_wparam(wparam, HIWORD(info->mouseData)), false});
      return suppress ? 1 : CallNextHookEx(nullptr, code, wparam, lparam);
    default:
      return CallNextHookEx(nullptr, code, wparam, lparam);
  }
}

LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam) {
  InputHook* self = g_instance;
  if (code < 0 || !self) return CallNextHookEx(nullptr, code, wparam, lparam);

  auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
  // See the matching check in mouse_proc: ignore SendInput-synthesized keys
  // so a same-machine Slave process's own injected keys don't get fought by
  // Master's hook.
  if (info->flags & LLKHF_INJECTED) return CallNextHookEx(nullptr, code, wparam, lparam);

  const bool down = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN);
  const bool extended = (info->flags & LLKHF_EXTENDED) != 0;
  self->pending_.keys.push_back({info->vkCode, info->scanCode, down, extended});

  if (down && info->vkCode == VK_ESCAPE) {
    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (ctrl && alt && shift) self->pending_.escape_pressed = true;
  }

  return self->suppress_.load() ? 1 : CallNextHookEx(nullptr, code, wparam, lparam);
}

}  // namespace
}  // namespace nockvm::input

#else  // !_WIN32

namespace nockvm::input {

InputHook::InputHook() = default;
InputHook::~InputHook() = default;
bool InputHook::install() { return false; }
void InputHook::uninstall() {}
void InputHook::suppress() {}
void InputHook::resume() {}
void InputHook::resume(int32_t, int32_t) {}
InputFrame InputHook::poll() { return InputFrame{}; }

}  // namespace nockvm::input

#endif
