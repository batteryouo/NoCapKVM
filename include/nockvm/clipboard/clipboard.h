#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace nockvm::clipboard {

enum class ContentType : uint8_t { Text, Jpeg };

struct ClipboardContent {
  ContentType type = ContentType::Text;
  std::vector<uint8_t> data;  // UTF-8 bytes for Text, JPEG-encoded bytes for Jpeg

  bool operator==(const ClipboardContent& other) const { return type == other.type && data == other.data; }
  bool operator!=(const ClipboardContent& other) const { return !(*this == other); }
};

// Reads whatever's currently on the OS clipboard. Text comes back as
// UTF-8; an image comes back re-encoded as JPEG (see jpeg_codec.h). Returns
// nullopt if the clipboard holds neither (or is empty, or -- for an image
// too large to fit even at the lowest quality step -- couldn't be encoded
// at all). Meant to be polled periodically (see app/clipboard_pump.cpp),
// not on every frame -- on X11 this round-trips through the selection-owner
// protocol, which is far more expensive than a plain memory read.
std::optional<ClipboardContent> read_clipboard();

// Overwrites the OS clipboard with the given content, decoding a Jpeg back
// into a format other native apps can actually paste (a Windows CF_DIB
// bitmap, or -- on X11 -- becoming the CLIPBOARD selection owner and
// serving PNG/UTF8_STRING to whoever asks).
void write_clipboard(const ClipboardContent& content);

// Services OS-level clipboard housekeeping that has to keep happening
// regardless of whether anything is currently Connected: on X11,
// responding to other apps' SelectionRequest while we're the CLIPBOARD
// owner (a no-op on Windows, which has no equivalent). Call this every
// frame unconditionally -- unlike read_clipboard()/write_clipboard(),
// which the app layer only calls while Connected and throttled to about
// once a second, content applied by an earlier write_clipboard() call
// keeps sitting on the clipboard (and this process keeps owning the
// selection on X11) even after disconnecting, and without this running
// independently, other apps' paste requests for it would never get a
// reply.
void pump_events();

}  // namespace nockvm::clipboard
