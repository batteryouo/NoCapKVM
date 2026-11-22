#include <cassert>
#include <cstdio>

#include "evdev_map.h"
#include "nockvm/input/protocol.h"

using namespace nockvm::input;

namespace {

void test_keymap() {
  // Plain keys pass through: Linux keycodes 1-88 equal Set 1 scancodes.
  assert(evdev_keycode(0x41, 0x1E, false) == 30);  // A -> KEY_A
  assert(evdev_keycode(0x7B, 0x58, false) == 88);  // F12 -> KEY_F12
  assert(evdev_keycode(0xC0, 0x29, false) == 41);  // grave / JIS Hankaku-Zenkaku -> KEY_GRAVE
  assert(evdev_keycode(0x26, 0x48, true) == evdev::kKeyUp);
  assert(evdev_keycode(0xA3, 0x1D, true) == evdev::kKeyRightCtrl);

  // PrintScreen, by vk and by its scancodes (Alt+PrintScreen reports 0x54).
  assert(evdev_keycode(0x2C, 0x37, true) == evdev::kKeySysRq);
  assert(evdev_keycode(0, 0x37, true) == evdev::kKeySysRq);
  assert(evdev_keycode(0, 0x54, false) == evdev::kKeySysRq);

  // Pause and NumLock share scancode 0x45; the vk decides.
  assert(evdev_keycode(0x13, 0x45, false) == evdev::kKeyPause);
  assert(evdev_keycode(0x90, 0x45, true) == evdev::kKeyNumLock);
  assert(evdev_keycode(0x03, 0x46, true) == evdev::kKeyPause);  // Ctrl+Pause (Break)

  // F13-F24 by vk, whatever the scancode.
  assert(evdev_keycode(0x7C, 0x64, false) == evdev::kKeyF13);
  assert(evdev_keycode(0x87, 0x76, false) == evdev::kKeyF13 + 11);
  assert(evdev_keycode(0x80, 0, false) == evdev::kKeyF13 + 4);  // F17 from macro software

  // Media and browser keys.
  assert(evdev_keycode(0xAF, 0x30, true) == evdev::kKeyVolumeUp);
  assert(evdev_keycode(0xAE, 0x2E, true) == evdev::kKeyVolumeDown);
  assert(evdev_keycode(0xAD, 0x20, true) == evdev::kKeyMute);
  assert(evdev_keycode(0xB3, 0x22, true) == evdev::kKeyPlayPause);
  assert(evdev_keycode(0xB0, 0x19, true) == evdev::kKeyNextSong);
  assert(evdev_keycode(0xB1, 0x10, true) == evdev::kKeyPreviousSong);
  assert(evdev_keycode(0xA6, 0x6A, true) == evdev::kKeyBack);

  // JIS and ABNT2.
  assert(evdev_keycode(0xF2, 0x70, false) == evdev::kKeyKatakanaHiragana);
  assert(evdev_keycode(0xC1, 0x73, false) == evdev::kKeyRo);
  assert(evdev_keycode(0x1C, 0x79, false) == evdev::kKeyHenkan);
  assert(evdev_keycode(0x1D, 0x7B, false) == evdev::kKeyMuhenkan);
  assert(evdev_keycode(0xDC, 0x7D, false) == evdev::kKeyYen);
  assert(evdev_keycode(0xC2, 0x7E, false) == evdev::kKeyKpComma);

  // Dropped: Power / Sleep / Wake, and codes with no meaning.
  assert(evdev_keycode(0x5F, 0x5F, true) == -1);  // VK_SLEEP
  assert(evdev_keycode(0, 0x5E, true) == -1);     // Power
  assert(evdev_keycode(0, 0x63, true) == -1);     // Wake
  assert(evdev_keycode(0, 0x2A, true) == -1);     // fake shift around PrintScreen
  assert(evdev_keycode(0, 0, false) == -1);
  assert(evdev_keycode(0, 0x200, false) == -1);
}

void test_wheel_accumulator() {
  // One notch per 120 units.
  {
    WheelAccumulator acc;
    assert(acc.add(120) == 1);
    assert(acc.add(-240) == -2);
    assert(acc.add(0) == 0);
  }
  // Small steps add up instead of being truncated away.
  {
    WheelAccumulator acc;
    int total = 0;
    for (int i = 0; i < 12; ++i) total += acc.add(30);
    assert(total == 3);
  }
  // A reversal drops the carried remainder.
  {
    WheelAccumulator acc;
    assert(acc.add(100) == 0);
    assert(acc.add(-100) == 0);
    // Without the reset, the -100 would only cancel the +100 and this
    // would not notch yet.
    assert(acc.add(-20) == -1);
  }
}

void test_wheel_protocol() {
  for (int16_t delta : {int16_t(120), int16_t(-120), int16_t(15), int16_t(-32768), int16_t(32767)}) {
    const auto payload = encode_mouse_wheel(delta);
    int16_t got = 0;
    assert(decode_mouse_wheel(payload.data(), payload.size(), got));
    assert(got == delta);
  }
  int16_t got = 0;
  const uint8_t short_payload[1] = {0};
  assert(!decode_mouse_wheel(short_payload, 1, got));
  // The horizontal wheel has its own type, distinct from every other message.
  static_assert(kMsgMouseHWheel != kMsgMouseWheel);
}

}  // namespace

int main() {
  test_keymap();
  test_wheel_accumulator();
  test_wheel_protocol();
  std::printf("All input tests passed.\n");
  return 0;
}
