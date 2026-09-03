#include "ico_decode.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace nockvm::app {
namespace {

uint16_t read_u16le(const std::vector<uint8_t>& b, size_t off) {
  return static_cast<uint16_t>(b[off]) | (static_cast<uint16_t>(b[off + 1]) << 8);
}

uint32_t read_u32le(const std::vector<uint8_t>& b, size_t off) {
  return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
         (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

int32_t read_i32le(const std::vector<uint8_t>& b, size_t off) { return static_cast<int32_t>(read_u32le(b, off)); }

struct DirEntry {
  int width = 0;
  int height = 0;
  int bit_count = 0;
  uint32_t bytes_in_res = 0;
  uint32_t image_offset = 0;
};

constexpr uint8_t kPngMagic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

bool is_png(const std::vector<uint8_t>& b, size_t off) {
  if (off + sizeof(kPngMagic) > b.size()) return false;
  return std::memcmp(b.data() + off, kPngMagic, sizeof(kPngMagic)) == 0;
}

// Decodes a raw BMP-style ICO frame (BITMAPINFOHEADER immediately followed
// by the pixel data, no BITMAPFILEHEADER -- that's specific to the ICO
// container). Only handles uncompressed 32bpp XOR data, which is what every
// modern icon-generation tool produces for a full-alpha icon; anything else
// bails out to nullopt rather than guessing.
std::optional<DecodedIcon> decode_bmp_frame(const std::vector<uint8_t>& b, const DirEntry& entry) {
  const size_t off = entry.image_offset;
  if (off + 40 > b.size()) return std::nullopt;
  const uint32_t header_size = read_u32le(b, off);
  if (header_size < 40) return std::nullopt;

  const int32_t bmp_width = read_i32le(b, off + 4);
  const int32_t bmp_height_x2 = read_i32le(b, off + 8);  // XOR + AND mask stacked
  const uint16_t bit_count = read_u16le(b, off + 14);
  const uint32_t compression = read_u32le(b, off + 16);

  if (bit_count != 32 || compression != 0 /* BI_RGB */) return std::nullopt;
  if (bmp_width <= 0 || bmp_height_x2 <= 0 || (bmp_height_x2 % 2) != 0) return std::nullopt;
  const int32_t height = bmp_height_x2 / 2;

  const size_t pixels_off = off + header_size;
  const size_t row_stride = static_cast<size_t>(bmp_width) * 4;  // always 4-byte aligned at 32bpp
  const size_t xor_bytes = row_stride * static_cast<size_t>(height);
  if (pixels_off + xor_bytes > b.size()) return std::nullopt;

  DecodedIcon out;
  out.width = bmp_width;
  out.height = height;
  out.argb32_be.resize(xor_bytes);

  // Source is bottom-up BGRA (file row 0 = image bottom); write top-down
  // ARGB into the output.
  for (int32_t y = 0; y < height; ++y) {
    const size_t src_row = pixels_off + row_stride * static_cast<size_t>(height - 1 - y);
    const size_t dst_row = row_stride * static_cast<size_t>(y);
    for (int32_t x = 0; x < bmp_width; ++x) {
      const uint8_t* src_px = b.data() + src_row + static_cast<size_t>(x) * 4;
      uint8_t* dst_px = out.argb32_be.data() + dst_row + static_cast<size_t>(x) * 4;
      const uint8_t blue = src_px[0], green = src_px[1], red = src_px[2], alpha = src_px[3];
      dst_px[0] = alpha;
      dst_px[1] = red;
      dst_px[2] = green;
      dst_px[3] = blue;
    }
  }
  return out;
}

}  // namespace

std::optional<DecodedIcon> load_ico_as_argb32(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return std::nullopt;
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (buf.size() < 6) return std::nullopt;

  if (read_u16le(buf, 0) != 0 || read_u16le(buf, 2) != 1) return std::nullopt;  // not an ICO
  const uint16_t count = read_u16le(buf, 4);
  if (count == 0 || 6 + static_cast<size_t>(count) * 16 > buf.size()) return std::nullopt;

  std::vector<DirEntry> entries;
  entries.reserve(count);
  for (uint16_t i = 0; i < count; ++i) {
    const size_t eoff = 6 + static_cast<size_t>(i) * 16;
    DirEntry e;
    e.width = buf[eoff] == 0 ? 256 : buf[eoff];
    e.height = buf[eoff + 1] == 0 ? 256 : buf[eoff + 1];
    e.bit_count = read_u16le(buf, eoff + 6);
    e.bytes_in_res = read_u32le(buf, eoff + 8);
    e.image_offset = read_u32le(buf, eoff + 12);
    entries.push_back(e);
  }

  // Prefer entries close to a modest tray-icon size, and among ties, the
  // one most likely to be a plain BMP frame (PNG compression is normally
  // reserved for large entries, so smaller candidates are tried first).
  std::sort(entries.begin(), entries.end(), [](const DirEntry& a, const DirEntry& b) {
    return std::abs(a.width - 32) < std::abs(b.width - 32);
  });

  for (const auto& e : entries) {
    if (e.image_offset + e.bytes_in_res > buf.size()) continue;
    if (is_png(buf, e.image_offset)) continue;
    if (auto decoded = decode_bmp_frame(buf, e)) return decoded;
  }
  return std::nullopt;
}

}  // namespace nockvm::app
