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

namespace {

void send_wheel(DWORD flag, int16_t delta) {
  INPUT input{};
  input.type = INPUT_MOUSE;
  input.mi.dwFlags = flag;
  input.mi.mouseData = static_cast<DWORD>(static_cast<int32_t>(delta));
  send(input);
}

}  // namespace

void inject_mouse_wheel(int16_t delta) { send_wheel(MOUSEEVENTF_WHEEL, delta); }
void inject_mouse_hwheel(int16_t delta) { send_wheel(MOUSEEVENTF_HWHEEL, delta); }

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

// Windows normalizes absolute coordinates against the virtual desktop.
void configure_pointer_bounds(int32_t, int32_t, int32_t, int32_t) {}

}  // namespace nockvm::input

#else  // !_WIN32

#include <cstdio>
#include <cstring>

#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

#include "evdev_map.h"

namespace nockvm::input {

// evdev_map.h spells the codes as numbers so it builds everywhere; check
// them against the real header here.
static_assert(evdev::kKeyKpEnter == KEY_KPENTER && evdev::kKeyRightCtrl == KEY_RIGHTCTRL);
static_assert(evdev::kKeyKpSlash == KEY_KPSLASH && evdev::kKeySysRq == KEY_SYSRQ);
static_assert(evdev::kKeyRightAlt == KEY_RIGHTALT && evdev::kKeyHome == KEY_HOME);
static_assert(evdev::kKeyUp == KEY_UP && evdev::kKeyPageUp == KEY_PAGEUP && evdev::kKeyLeft == KEY_LEFT);
static_assert(evdev::kKeyRight == KEY_RIGHT && evdev::kKeyEnd == KEY_END && evdev::kKeyDown == KEY_DOWN);
static_assert(evdev::kKeyPageDown == KEY_PAGEDOWN && evdev::kKeyInsert == KEY_INSERT);
static_assert(evdev::kKeyDelete == KEY_DELETE && evdev::kKeyMute == KEY_MUTE);
static_assert(evdev::kKeyVolumeDown == KEY_VOLUMEDOWN && evdev::kKeyVolumeUp == KEY_VOLUMEUP);
static_assert(evdev::kKeyKpEqual == KEY_KPEQUAL && evdev::kKeyPause == KEY_PAUSE);
static_assert(evdev::kKeyKpComma == KEY_KPCOMMA && evdev::kKeyLeftMeta == KEY_LEFTMETA);
static_assert(evdev::kKeyRightMeta == KEY_RIGHTMETA && evdev::kKeyCompose == KEY_COMPOSE);
static_assert(evdev::kKeyStop == KEY_STOP && evdev::kKeyCalc == KEY_CALC && evdev::kKeyMail == KEY_MAIL);
static_assert(evdev::kKeyBookmarks == KEY_BOOKMARKS && evdev::kKeyComputer == KEY_COMPUTER);
static_assert(evdev::kKeyBack == KEY_BACK && evdev::kKeyForward == KEY_FORWARD);
static_assert(evdev::kKeyNextSong == KEY_NEXTSONG && evdev::kKeyPlayPause == KEY_PLAYPAUSE);
static_assert(evdev::kKeyPreviousSong == KEY_PREVIOUSSONG && evdev::kKeyStopCd == KEY_STOPCD);
static_assert(evdev::kKeyHomepage == KEY_HOMEPAGE && evdev::kKeyRefresh == KEY_REFRESH);
static_assert(evdev::kKeyF13 == KEY_F13 && evdev::kKeyF13 + 11 == KEY_F24);
static_assert(evdev::kKeySearch == KEY_SEARCH && evdev::kKeyMedia == KEY_MEDIA);
static_assert(evdev::kKeyNumLock == KEY_NUMLOCK && evdev::kKeyRo == KEY_RO);
static_assert(evdev::kKeyHenkan == KEY_HENKAN && evdev::kKeyKatakanaHiragana == KEY_KATAKANAHIRAGANA);
static_assert(evdev::kKeyMuhenkan == KEY_MUHENKAN && evdev::kKeyYen == KEY_YEN);

namespace {

int32_t g_bounds_min_x = 0, g_bounds_max_x = 65535, g_bounds_min_y = 0, g_bounds_max_y = 65535;
uint8_t g_modifier_state = 0;  // shadow of held Shift/Ctrl/Alt/Meta -- uinput is write-only, no readback

void emit(libevdev_uinput* dev, uint16_t type, uint16_t code, int32_t value) {
  if (!dev) return;
  libevdev_uinput_write_event(dev, type, code, value);
}

void sync_report(libevdev_uinput* dev) { emit(dev, EV_SYN, SYN_REPORT, 0); }

// Creates a managed uinput device with the requested capabilities.
libevdev_uinput* create_device(const char* name, void (*configure)(libevdev*)) {
  libevdev* dev = libevdev_new();
  libevdev_set_name(dev, name);
  configure(dev);

  libevdev_uinput* uidev = nullptr;
  const int rc = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev);
  libevdev_free(dev);
  if (rc < 0) {
    // uinput availability can change while the application is running.
    std::fprintf(stderr, "nockvm: failed to create uinput device \"%s\": %s (check /dev/uinput permissions)\n", name,
                 std::strerror(-rc));
    return nullptr;
  }
  return uidev;
}

// Retry creation until uinput becomes available; a successful device lives for the process lifetime.
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
    // Classify the absolute-axis device as a pointer for libinput.
    libevdev_enable_property(d, INPUT_PROP_POINTER);

    libevdev_enable_event_type(d, EV_KEY);
    libevdev_enable_event_code(d, EV_KEY, BTN_LEFT, nullptr);
    libevdev_enable_event_code(d, EV_KEY, BTN_RIGHT, nullptr);
    libevdev_enable_event_code(d, EV_KEY, BTN_MIDDLE, nullptr);
    libevdev_enable_event_code(d, EV_KEY, BTN_SIDE, nullptr);
    libevdev_enable_event_code(d, EV_KEY, BTN_EXTRA, nullptr);

    libevdev_enable_event_type(d, EV_REL);
    libevdev_enable_event_code(d, EV_REL, REL_WHEEL, nullptr);
    libevdev_enable_event_code(d, EV_REL, REL_HWHEEL, nullptr);
#ifdef REL_WHEEL_HI_RES
    // libinput uses these when present, which keeps touchpad and
    // high-resolution scrolling from Master smooth.
    libevdev_enable_event_code(d, EV_REL, REL_WHEEL_HI_RES, nullptr);
    libevdev_enable_event_code(d, EV_REL, REL_HWHEEL_HI_RES, nullptr);
#endif

    libevdev_enable_event_type(d, EV_ABS);
    const input_absinfo abs_x{0, g_bounds_min_x, g_bounds_max_x, 0, 0, 0};
    const input_absinfo abs_y{0, g_bounds_min_y, g_bounds_max_y, 0, 0, 0};
    libevdev_enable_event_code(d, EV_ABS, ABS_X, &abs_x);
    libevdev_enable_event_code(d, EV_ABS, ABS_Y, &abs_y);
  });
  return dev;
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

namespace {

WheelAccumulator g_wheel, g_hwheel;  // only touched from the Slave's receive thread

// Sends the high-resolution value (also 120 per notch, so it passes through
// unchanged) plus whole notches for readers of plain REL_WHEEL / REL_HWHEEL,
// the same pair a real high-resolution mouse reports. hi_res_code < 0 means
// the kernel headers predate high-resolution scrolling.
void emit_wheel(WheelAccumulator& acc, uint16_t notch_code, int hi_res_code, int16_t delta) {
  auto* dev = ensure_mouse_dev();
  if (hi_res_code >= 0) emit(dev, EV_REL, static_cast<uint16_t>(hi_res_code), delta);
  const int notches = acc.add(delta);
  if (notches != 0) emit(dev, EV_REL, notch_code, notches);
  sync_report(dev);
}

#ifdef REL_WHEEL_HI_RES
constexpr int kWheelHiRes = REL_WHEEL_HI_RES;
constexpr int kHWheelHiRes = REL_HWHEEL_HI_RES;
#else
constexpr int kWheelHiRes = -1;
constexpr int kHWheelHiRes = -1;
#endif

}  // namespace

void inject_mouse_wheel(int16_t delta) { emit_wheel(g_wheel, REL_WHEEL, kWheelHiRes, delta); }
void inject_mouse_hwheel(int16_t delta) { emit_wheel(g_hwheel, REL_HWHEEL, kHWheelHiRes, delta); }

void inject_key(uint32_t vk, uint32_t scancode, bool down, bool extended) {
  auto* dev = ensure_keyboard_dev();
  const int code = evdev_keycode(vk, scancode, extended);
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
