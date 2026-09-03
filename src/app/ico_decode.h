#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nockvm::app {

struct DecodedIcon {
  int32_t width = 0;
  int32_t height = 0;
  // width*height*4 bytes, row-major, top row first, each pixel as four
  // bytes [A, R, G, B] -- the byte layout org.kde.StatusNotifierItem's
  // IconPixmap property expects ("ARGB32 in network byte order").
  std::vector<uint8_t> argb32_be;
};

// Picks the frame closest to a modest tray-icon size (32px) from a Windows
// .ico file and decodes it, converting from the embedded DIB's bottom-up
// BGRA layout to IconPixmap's top-down big-endian ARGB. Returns nullopt on
// any parse failure, if the file doesn't exist, or if every candidate frame
// turns out to be PNG-compressed (common only for large 256px entries;
// decoding those isn't implemented here) -- callers should fall back to a
// named icon-theme icon in that case rather than treating this as fatal.
std::optional<DecodedIcon> load_ico_as_argb32(const std::string& path);

}  // namespace nockvm::app
