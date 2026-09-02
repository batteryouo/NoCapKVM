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

void InputHook::set_suppress(bool suppress) { suppress_.store(suppress); }

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
void InputHook::set_suppress(bool) {}
InputFrame InputHook::poll() { return InputFrame{}; }

}  // namespace nockvm::input

#endif
