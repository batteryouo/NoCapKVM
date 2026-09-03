#include "nockvm/clipboard/jpeg_codec.h"
#include "stb_image.h"
#include "stb_image_write.h"

namespace nockvm::clipboard {
namespace {

void append_to_vector(void* context, void* data, int size) {
  auto* out = reinterpret_cast<std::vector<uint8_t>*>(context);
  const auto* bytes = reinterpret_cast<const uint8_t*>(data);
  out->insert(out->end(), bytes, bytes + size);
}

}  // namespace

std::vector<uint8_t> encode_jpeg(const uint8_t* rgb, int width, int height, int quality, size_t max_bytes) {
  for (int q = quality; q >= 20; q -= 15) {
    std::vector<uint8_t> out;
    if (!stbi_write_jpg_to_func(append_to_vector, &out, width, height, 3, rgb, q)) return {};
    if (out.size() <= max_bytes) return out;
  }
  return {};
}

std::vector<uint8_t> encode_png(const uint8_t* rgb, int width, int height) {
  std::vector<uint8_t> out;
  stbi_write_png_to_func(append_to_vector, &out, width, height, 3, rgb, width * 3);
  return out;
}

bool decode_jpeg(const uint8_t* data, size_t len, std::vector<uint8_t>& out_rgb, int& width, int& height) {
  int channels = 0;
  unsigned char* pixels =
      stbi_load_from_memory(data, static_cast<int>(len), &width, &height, &channels, 3);
  if (!pixels) return false;
  out_rgb.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
  stbi_image_free(pixels);
  return true;
}

}  // namespace nockvm::clipboard
