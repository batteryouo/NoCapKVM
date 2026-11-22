#pragma once
#include <cstdint>

// Translation from what the Windows Master captures (WH_KEYBOARD_LL vk,
// scancode, extended flag; WHEEL_DELTA-based wheel deltas) to Linux evdev
// codes for the uinput Slave. Kept free of platform headers so it builds
// and is tested on every platform; inject.cpp checks the numeric codes
// against <linux/input-event-codes.h> at compile time.

namespace nockvm::input {

// evdev codes used by the table (values from linux/input-event-codes.h,
// a stable kernel ABI).
namespace evdev {
constexpr int kKeyKpEnter = 96;
constexpr int kKeyRightCtrl = 97;
constexpr int kKeyKpSlash = 98;
constexpr int kKeySysRq = 99;
constexpr int kKeyRightAlt = 100;
constexpr int kKeyHome = 102;
constexpr int kKeyUp = 103;
constexpr int kKeyPageUp = 104;
constexpr int kKeyLeft = 105;
constexpr int kKeyRight = 106;
constexpr int kKeyEnd = 107;
constexpr int kKeyDown = 108;
constexpr int kKeyPageDown = 109;
constexpr int kKeyInsert = 110;
constexpr int kKeyDelete = 111;
constexpr int kKeyMute = 113;
constexpr int kKeyVolumeDown = 114;
constexpr int kKeyVolumeUp = 115;
constexpr int kKeyKpEqual = 117;
constexpr int kKeyPause = 119;
constexpr int kKeyKpComma = 121;
constexpr int kKeyLeftMeta = 125;
constexpr int kKeyRightMeta = 126;
constexpr int kKeyCompose = 127;
constexpr int kKeyStop = 128;
constexpr int kKeyCalc = 140;
constexpr int kKeyMail = 155;
constexpr int kKeyBookmarks = 156;
constexpr int kKeyComputer = 157;
constexpr int kKeyBack = 158;
constexpr int kKeyForward = 159;
constexpr int kKeyNextSong = 163;
constexpr int kKeyPlayPause = 164;
constexpr int kKeyPreviousSong = 165;
constexpr int kKeyStopCd = 166;
constexpr int kKeyHomepage = 172;
constexpr int kKeyRefresh = 173;
constexpr int kKeyF13 = 183;  // F13..F24 are consecutive, through 194
constexpr int kKeySearch = 217;
constexpr int kKeyMedia = 226;
constexpr int kKeyNumLock = 69;
// JIS / ABNT2
constexpr int kKeyRo = 89;
constexpr int kKeyHenkan = 92;
constexpr int kKeyKatakanaHiragana = 93;
constexpr int kKeyMuhenkan = 94;
constexpr int kKeyYen = 124;
}  // namespace evdev

// Linux keycode for a key captured on Windows, or -1 to drop it.
// Power, Sleep and Wake are deliberately dropped: forwarding them would
// suspend or power off the Slave from a key meant for the Master.
int evdev_keycode(uint32_t vk, uint32_t scancode, bool extended);

// Windows reports wheel movement in units of 120 per notch, and a
// high-resolution wheel or a touchpad sends smaller steps. evdev's
// REL_WHEEL counts whole notches, so the remainder is carried until it adds
// up to one. A change of direction drops the carried remainder, so a
// reversal takes effect at once (the kernel's HID driver does the same).
class WheelAccumulator {
public:
  static constexpr int kUnitsPerNotch = 120;
  // Whole notches to report for this delta; positive is up / right.
  int add(int16_t delta) {
    if (delta == 0) return 0;
    if (remainder_ != 0 && (remainder_ > 0) != (delta > 0)) remainder_ = 0;
    remainder_ += delta;
    const int notches = remainder_ / kUnitsPerNotch;
    remainder_ -= notches * kUnitsPerNotch;
    return notches;
  }

private:
  int remainder_ = 0;
};

}  // namespace nockvm::input
