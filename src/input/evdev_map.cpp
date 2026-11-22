#include "evdev_map.h"

namespace nockvm::input {
namespace {

// Windows virtual-key codes, numerically (no windows.h here).
constexpr uint32_t kVkPause = 0x13;
constexpr uint32_t kVkCancel = 0x03;  // Ctrl+Pause (Break)
constexpr uint32_t kVkSnapshot = 0x2C;
constexpr uint32_t kVkNumLock = 0x90;
constexpr uint32_t kVkF13 = 0x7C;
constexpr uint32_t kVkF24 = 0x87;

// Keys whose virtual-key code identifies them more reliably than their
// scancode. Media keys come from a separate HID usage page, and macro
// software often sends F13-F24 with an arbitrary scancode. Pause and
// NumLock share scancode 0x45 and only the vk tells them apart (the
// low-level hook even flags NumLock as extended and Pause as not).
int from_vk(uint32_t vk) {
  if (vk >= kVkF13 && vk <= kVkF24) return evdev::kKeyF13 + static_cast<int>(vk - kVkF13);
  switch (vk) {
    case kVkPause:
    case kVkCancel: return evdev::kKeyPause;
    case kVkSnapshot: return evdev::kKeySysRq;
    case kVkNumLock: return evdev::kKeyNumLock;
    case 0xA6: return evdev::kKeyBack;           // VK_BROWSER_BACK
    case 0xA7: return evdev::kKeyForward;        // VK_BROWSER_FORWARD
    case 0xA8: return evdev::kKeyRefresh;        // VK_BROWSER_REFRESH
    case 0xA9: return evdev::kKeyStop;           // VK_BROWSER_STOP
    case 0xAA: return evdev::kKeySearch;         // VK_BROWSER_SEARCH
    case 0xAB: return evdev::kKeyBookmarks;      // VK_BROWSER_FAVORITES
    case 0xAC: return evdev::kKeyHomepage;       // VK_BROWSER_HOME
    case 0xAD: return evdev::kKeyMute;           // VK_VOLUME_MUTE
    case 0xAE: return evdev::kKeyVolumeDown;     // VK_VOLUME_DOWN
    case 0xAF: return evdev::kKeyVolumeUp;       // VK_VOLUME_UP
    case 0xB0: return evdev::kKeyNextSong;       // VK_MEDIA_NEXT_TRACK
    case 0xB1: return evdev::kKeyPreviousSong;   // VK_MEDIA_PREV_TRACK
    case 0xB2: return evdev::kKeyStopCd;         // VK_MEDIA_STOP
    case 0xB3: return evdev::kKeyPlayPause;      // VK_MEDIA_PLAY_PAUSE
    case 0xB4: return evdev::kKeyMail;           // VK_LAUNCH_MAIL
    case 0xB5: return evdev::kKeyMedia;          // VK_LAUNCH_MEDIA_SELECT
    case 0xB6: return evdev::kKeyComputer;       // VK_LAUNCH_APP1
    case 0xB7: return evdev::kKeyCalc;           // VK_LAUNCH_APP2
    default: return -1;
  }
}

// E0-prefixed keys got distinct, non-formulaic keycodes on Linux.
int from_extended_scancode(uint32_t scancode) {
  switch (scancode) {
    case 0x1C: return evdev::kKeyKpEnter;
    case 0x1D: return evdev::kKeyRightCtrl;
    case 0x35: return evdev::kKeyKpSlash;
    case 0x37: return evdev::kKeySysRq;  // PrintScreen
    case 0x38: return evdev::kKeyRightAlt;
    case 0x46: return evdev::kKeyPause;  // Ctrl+Pause (Break)
    case 0x47: return evdev::kKeyHome;
    case 0x48: return evdev::kKeyUp;
    case 0x49: return evdev::kKeyPageUp;
    case 0x4B: return evdev::kKeyLeft;
    case 0x4D: return evdev::kKeyRight;
    case 0x4F: return evdev::kKeyEnd;
    case 0x50: return evdev::kKeyDown;
    case 0x51: return evdev::kKeyPageDown;
    case 0x52: return evdev::kKeyInsert;
    case 0x53: return evdev::kKeyDelete;
    case 0x5B: return evdev::kKeyLeftMeta;
    case 0x5C: return evdev::kKeyRightMeta;
    case 0x5D: return evdev::kKeyCompose;
    default: return -1;
  }
}

int from_scancode(uint32_t scancode) {
  switch (scancode) {
    case 0x54: return evdev::kKeySysRq;  // Alt+PrintScreen
    case 0x59: return evdev::kKeyKpEqual;
    case 0x70: return evdev::kKeyKatakanaHiragana;
    case 0x73: return evdev::kKeyRo;  // JIS Ro, ABNT2 /?
    case 0x79: return evdev::kKeyHenkan;
    case 0x7B: return evdev::kKeyMuhenkan;
    case 0x7D: return evdev::kKeyYen;
    case 0x7E: return evdev::kKeyKpComma;  // ABNT2 keypad .
    default: break;
  }
  // Linux keycodes 1-88 equal the PC/XT Set 1 scancodes Windows reports.
  // That includes the JIS Hankaku/Zenkaku key (0x29, KEY_GRAVE), which a
  // jp XKB layout maps to Zenkaku_Hankaku. 84 is not a Linux key.
  if (scancode >= 1 && scancode <= 88 && scancode != 0x54) return static_cast<int>(scancode);
  return -1;
}

}  // namespace

int evdev_keycode(uint32_t vk, uint32_t scancode, bool extended) {
  const int by_vk = from_vk(vk);
  if (by_vk >= 0) return by_vk;
  return extended ? from_extended_scancode(scancode) : from_scancode(scancode);
}

}  // namespace nockvm::input
