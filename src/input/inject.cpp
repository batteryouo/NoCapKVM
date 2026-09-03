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

bool get_local_cursor_pos(int32_t& x, int32_t& y) {
  POINT pt;
  if (!GetCursorPos(&pt)) return false;
  x = pt.x;
  y = pt.y;
  return true;
}

// SendInput's own GetSystemMetrics-based normalization is used instead; see
// the Linux branch below, which needs this to size its virtual device.
void configure_pointer_bounds(int32_t, int32_t, int32_t, int32_t) {}

}  // namespace nockvm::input

#else  // !_WIN32

#include <cstdio>
#include <cstring>

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

namespace nockvm::input {
namespace {

int32_t g_bounds_min_x = 0, g_bounds_max_x = 65535, g_bounds_min_y = 0, g_bounds_max_y = 65535;
uint8_t g_modifier_state = 0;  // shadow of held Shift/Ctrl/Alt/Meta -- uinput is write-only, no readback

void emit(libevdev_uinput* dev, uint16_t type, uint16_t code, int32_t value) {
  if (!dev) return;
  libevdev_uinput_write_event(dev, type, code, value);
}

void sync_report(libevdev_uinput* dev) { emit(dev, EV_SYN, SYN_REPORT, 0); }

// Builds the device via libevdev_new()/enable_event_code() and hands it to
// libevdev_uinput_create_from_device(), which opens /dev/uinput itself
// (LIBEVDEV_UINPUT_OPEN_MANAGED) and does the UI_DEV_SETUP/UI_DEV_CREATE
// ioctl sequence internally -- a thin wrapper around the same kernel API,
// maintained upstream by the same people who maintain libinput, so it's
// preferred here over hand-rolling the ioctls directly.
libevdev_uinput* create_device(const char* name, void (*configure)(libevdev*)) {
  libevdev* dev = libevdev_new();
  libevdev_set_name(dev, name);
  configure(dev);

  libevdev_uinput* uidev = nullptr;
  const int rc = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  libevdev_free(dev);
  if (rc < 0) {
    // Silently swallowing this used to mean every subsequent inject_*()
    // call on this device dropped its event forever with zero visible
    // sign anything was wrong -- if this fails once (e.g. a transient
    // permissions/module-load race right at startup) print it so a
    // real-hardware repro is actually diagnosable from the console.
    std::fprintf(stderr, "nockvm: failed to create uinput device \"%s\": %s (check /dev/uinput permissions)\n", name,
                 std::strerror(-rc));
    return nullptr;
  }
  return uidev;
}

// Lazily created, kept open for the process's lifetime once it succeeds --
// the kernel tears the virtual device down when this closes at process
// exit. Deliberately retries on every call while dev is still null (not
// cached as a permanent failure): the one real failure mode seen in
// practice is a transient race at process start, not a persistent one, so
// giving up forever after a single failed attempt would turn a one-off
// hiccup into "input never works again until the whole app restarts".
libevdev_uinput* ensure_keyboard_dev() {
  static libevdev_uinput* dev = nullptr;
  if (dev) return dev;
  dev = create_device("NoCapKVM Virtual Keyboard", [](libevdev* d) {
    libevdev_enable_event_type(d, EV_KEY);
    for (int code = 0; code <= KEY_MAX; ++code) libevdev_enable_event_code(d, EV_KEY, code, nullptr);
  });
  return dev;
}

libevdev_uinput* ensure_mouse_dev() {
  static libevdev_uinput* dev = nullptr;
  if (dev) return dev;
  dev = create_device("NoCapKVM Virtual Mouse", [](libevdev* d) {
    // Without this, libinput has nothing telling it this absolute-axis
    // device is a pointer rather than e.g. a touchscreen/tablet, which is
    // a real source of inconsistent behavior across compositors -- some
    // still move the cursor without it, some don't.
    libevdev_enable_property(d, INPUT_PROP_POINTER);

    libevdev_enable_event_type(d, EV_KEY);
    libevdev_enable_event_code(d, EV_KEY, BTN_LEFT, nullptr);
    libevdev_enable_event_code(d, EV_KEY, BTN_RIGHT, nullptr);
    libevdev_enable_event_code(d, EV_KEY, BTN_MIDDLE, nullptr);
    libevdev_enable_event_code(d, EV_KEY, BTN_SIDE, nullptr);
    libevdev_enable_event_code(d, EV_KEY, BTN_EXTRA, nullptr);

    libevdev_enable_event_type(d, EV_REL);
    libevdev_enable_event_code(d, EV_REL, REL_WHEEL, nullptr);

    libevdev_enable_event_type(d, EV_ABS);
    const input_absinfo abs_x{0, g_bounds_min_x, g_bounds_max_x, 0, 0, 0};
    const input_absinfo abs_y{0, g_bounds_min_y, g_bounds_max_y, 0, 0, 0};
    libevdev_enable_event_code(d, EV_ABS, ABS_X, &abs_x);
    libevdev_enable_event_code(d, EV_ABS, ABS_Y, &abs_y);
  });
  return dev;
}

// Windows scancodes and Linux evdev keycodes both derive from the legacy
// PC/XT "Set 1" scancode table, and Linux keycodes 0-88 are defined to
// equal those scancodes directly -- so the base region needs no table.
// Extended (E0-prefixed) keys got distinct, non-formulaic keycode numbers
// on the Linux side and need an explicit lookup; anything not listed here
// is silently dropped rather than mapped to something wrong.
int translate_extended_scancode(uint32_t scancode) {
  switch (scancode) {
    case 0x1C: return KEY_KPENTER;
    case 0x1D: return KEY_RIGHTCTRL;
    case 0x35: return KEY_KPSLASH;
    case 0x38: return KEY_RIGHTALT;
    case 0x47: return KEY_HOME;
    case 0x48: return KEY_UP;
    case 0x49: return KEY_PAGEUP;
    case 0x4B: return KEY_LEFT;
    case 0x4D: return KEY_RIGHT;
    case 0x4F: return KEY_END;
    case 0x50: return KEY_DOWN;
    case 0x51: return KEY_PAGEDOWN;
    case 0x52: return KEY_INSERT;
    case 0x53: return KEY_DELETE;
    case 0x5B: return KEY_LEFTMETA;
    case 0x5C: return KEY_RIGHTMETA;
    case 0x5D: return KEY_COMPOSE;
    default: return -1;
  }
}

int modifier_bit(int code) {
  switch (code) {
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT: return 0;
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL: return 1;
    case KEY_LEFTALT:
    case KEY_RIGHTALT: return 2;
    case KEY_LEFTMETA:
    case KEY_RIGHTMETA: return 3;
    default: return -1;
  }
}

}  // namespace

void inject_mouse_absolute(int32_t x, int32_t y) {
  auto* dev = ensure_mouse_dev();
  emit(dev, EV_ABS, ABS_X, x);
  emit(dev, EV_ABS, ABS_Y, y);
  sync_report(dev);
}

void inject_mouse_button(uint8_t button, bool down) {
  auto* dev = ensure_mouse_dev();
  int code;
  switch (button) {
    case 0: code = BTN_LEFT; break;
    case 1: code = BTN_RIGHT; break;
    case 2: code = BTN_MIDDLE; break;
    case 3: code = BTN_SIDE; break;
    case 4: code = BTN_EXTRA; break;
    default: return;
  }
  emit(dev, EV_KEY, code, down ? 1 : 0);
  sync_report(dev);
}

void inject_mouse_wheel(int16_t delta) {
  auto* dev = ensure_mouse_dev();
  emit(dev, EV_REL, REL_WHEEL, delta / 120);  // Windows' WHEEL_DELTA=120 units -> Linux's one-notch-per-unit
  sync_report(dev);
}

void inject_key(uint32_t /*vk*/, uint32_t scancode, bool down, bool extended) {
  auto* dev = ensure_keyboard_dev();
  const int code = extended ? translate_extended_scancode(scancode)
                             : (scancode >= 1 && scancode <= 88 ? static_cast<int>(scancode) : -1);
  if (code < 0) return;

  emit(dev, EV_KEY, code, down ? 1 : 0);
  sync_report(dev);

  const int bit = modifier_bit(code);
  if (bit >= 0) {
    if (down) g_modifier_state |= static_cast<uint8_t>(1u << bit);
    else g_modifier_state &= static_cast<uint8_t>(~(1u << bit));
  }
}

void set_modifiers(uint8_t mask) {
  auto* dev = ensure_keyboard_dev();
  struct { int code; uint8_t bit; } kModifiers[] = {
      {KEY_LEFTSHIFT, 0}, {KEY_LEFTCTRL, 1}, {KEY_LEFTALT, 2}, {KEY_LEFTMETA, 3}};
  bool changed = false;
  for (const auto& m : kModifiers) {
    const bool want_down = (mask & (1u << m.bit)) != 0;
    const bool is_down = (g_modifier_state & (1u << m.bit)) != 0;
    if (want_down == is_down) continue;
    emit(dev, EV_KEY, m.code, want_down ? 1 : 0);
    if (want_down) g_modifier_state |= (1u << m.bit);
    else g_modifier_state &= static_cast<uint8_t>(~(1u << m.bit));
    changed = true;
  }
  if (changed) sync_report(dev);
}

bool get_local_cursor_pos(int32_t&, int32_t&) { return false; }

void configure_pointer_bounds(int32_t min_x, int32_t max_x, int32_t min_y, int32_t max_y) {
  g_bounds_min_x = min_x;
  g_bounds_max_x = max_x;
  g_bounds_min_y = min_y;
  g_bounds_max_y = max_y;
}

}  // namespace nockvm::input

#endif
