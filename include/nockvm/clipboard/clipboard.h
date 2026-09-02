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

}  // namespace nockvm::clipboard
