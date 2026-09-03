#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nockvm::clipboard {

// Encodes a top-down, unpadded RGB buffer (3 bytes/pixel, row-major) to
// JPEG. Starts at `quality` and, if the result would exceed max_bytes,
// retries at progressively lower quality (down to a floor of 20) before
// giving up and returning an empty vector -- callers send this over a
// SecureChannel frame with a hard size cap (see kMaxFrameLen in
// secure_channel.cpp), so silently producing an oversized buffer would
// just fail later, further from the cause.
std::vector<uint8_t> encode_jpeg(const uint8_t* rgb, int width, int height, int quality, size_t max_bytes);

// Decodes image bytes (JPEG, PNG, or anything else stb_image recognizes --
// it auto-detects the container format from the header regardless of this
// function's name) back into a top-down, unpadded RGB buffer. Returns
// false (leaving out_rgb/width/height untouched) on malformed input.
bool decode_jpeg(const uint8_t* data, size_t len, std::vector<uint8_t>& out_rgb, int& width, int& height);

// Encodes the same kind of buffer to PNG instead -- used only by the X11
// clipboard implementation, which needs to re-serve an incoming Jpeg as
// PNG for apps that only paste that format (see clipboard.cpp).
std::vector<uint8_t> encode_png(const uint8_t* rgb, int width, int height);

}  // namespace nockvm::clipboard
