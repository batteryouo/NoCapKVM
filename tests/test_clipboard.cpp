#include <cassert>
#include <cstdlib>
#include "nockvm/clipboard/jpeg_codec.h"

using namespace nockvm::clipboard;

namespace {

std::vector<uint8_t> make_solid_rgb(int width, int height, uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
  for (size_t i = 0; i < rgb.size(); i += 3) {
    rgb[i] = r;
    rgb[i + 1] = g;
    rgb[i + 2] = b;
  }
  return rgb;
}

int abs_diff(int a, int b) { return std::abs(a - b); }

}  // namespace

int main() {
  // JPEG round-trip: lossy, but a flat solid color survives compression
  // close enough to still be recognizable.
  {
    const std::vector<uint8_t> rgb = make_solid_rgb(16, 16, 200, 40, 40);
    const std::vector<uint8_t> jpeg = encode_jpeg(rgb.data(), 16, 16, 85, 100000);
    assert(!jpeg.empty());

    std::vector<uint8_t> decoded;
    int width = 0, height = 0;
    assert(decode_jpeg(jpeg.data(), jpeg.size(), decoded, width, height));
    assert(width == 16 && height == 16);
    assert(decoded.size() == rgb.size());

    const size_t center = decoded.size() / 2 - (decoded.size() / 2) % 3;
    assert(abs_diff(decoded[center + 0], 200) < 20);
    assert(abs_diff(decoded[center + 1], 40) < 20);
    assert(abs_diff(decoded[center + 2], 40) < 20);
  }

  // A size budget too small for even the lowest quality step fails
  // cleanly (empty vector) rather than silently producing an oversized
  // payload the caller's SecureChannel frame cap would reject anyway.
  {
    const std::vector<uint8_t> rgb = make_solid_rgb(64, 64, 10, 20, 30);
    const std::vector<uint8_t> jpeg = encode_jpeg(rgb.data(), 64, 64, 85, 1);
    assert(jpeg.empty());
  }

  // PNG is lossless -- round-trips exactly. Used by the X11 clipboard path
  // to re-serve an incoming Jpeg to apps that only accept image/png.
  {
    const std::vector<uint8_t> rgb = make_solid_rgb(8, 8, 5, 250, 128);
    const std::vector<uint8_t> png = encode_png(rgb.data(), 8, 8);
    assert(!png.empty());

    std::vector<uint8_t> decoded;
    int width = 0, height = 0;
    // decode_jpeg is named for its primary use but decodes anything
    // stb_image recognizes, PNG included -- see its header comment.
    assert(decode_jpeg(png.data(), png.size(), decoded, width, height));
    assert(width == 8 && height == 8);
    assert(decoded == rgb);
  }

  return 0;
}
