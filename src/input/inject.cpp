#include "nockvm/input/inject.h"

#ifdef _WIN32
#include <windows.h>

namespace nockvm::input {
namespace {

void send(INPUT& input) { SendInput(1, &input, sizeof(INPUT)); }

}  // namespace

void inject_mouse_absolute(int32_t x, int32_t y) {
  const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (vw <= 0 || vh <= 0) return;

  INPUT input{};
  input.type = INPUT_MOUSE;
  input.mi.dx = static_cast<LONG>((static_cast<double>(x - vx) * 65535.0) / (vw - 1));
  input.mi.dy = static_cast<LONG>((static_cast<double>(y - vy) * 65535.0) / (vh - 1));
  input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  send(input);
}

void inject_mouse_button(uint8_t button, bool down) {
  INPUT input{};
  input.type = INPUT_MOUSE;
  switch (button) {
    case 0: input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
    case 1: input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
    case 2: input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    case 3:
    case 4:
      input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
      input.mi.mouseData = button == 3 ? XBUTTON1 : XBUTTON2;
      break;
    default: return;
  }
  send(input);
}

void inject_mouse_wheel(int16_t delta) {
  INPUT input{};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = MOUSEEVENTF_WHEEL;
  input.mi.mouseData = static_cast<DWORD>(static_cast<int32_t>(delta));
  send(input);
}

void inject_key(uint32_t vk, uint32_t scancode, bool down, bool extended) {
  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = static_cast<WORD>(vk);
  input.ki.wScan = static_cast<WORD>(scancode);
  input.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
  send(input);
}

void set_modifiers(uint8_t mask) {
  struct { int vk; uint8_t bit; } kModifiers[] = {
      {VK_SHIFT, 0}, {VK_CONTROL, 1}, {VK_MENU, 2}, {VK_LWIN, 3}};
  for (const auto& m : kModifiers) {
    const bool want_down = (mask & (1u << m.bit)) != 0;
    const bool is_down = (GetAsyncKeyState(m.vk) & 0x8000) != 0;
    if (want_down != is_down) {
      INPUT input{};
      input.type = INPUT_KEYBOARD;
      input.ki.wVk = static_cast<WORD>(m.vk);
      input.ki.dwFlags = want_down ? 0 : KEYEVENTF_KEYUP;
      send(input);
    }
  }
}

void set_local_cursor_pos(int32_t x, int32_t y) { SetCursorPos(x, y); }

bool get_local_cursor_pos(int32_t& x, int32_t& y) {
  POINT pt;
  if (!GetCursorPos(&pt)) return false;
  x = pt.x;
  y = pt.y;
  return true;
}

}  // namespace nockvm::input

#else  // !_WIN32

namespace nockvm::input {

void inject_mouse_absolute(int32_t, int32_t) {}
void inject_mouse_button(uint8_t, bool) {}
void inject_mouse_wheel(int16_t) {}
void inject_key(uint32_t, uint32_t, bool, bool) {}
void set_modifiers(uint8_t) {}
void set_local_cursor_pos(int32_t, int32_t) {}
bool get_local_cursor_pos(int32_t&, int32_t&) { return false; }

}  // namespace nockvm::input

#endif
