#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace nockvm::audio {

// Largest sample magnitude in a PCM buffer, in parts per million of full
// scale (0 = digital silence, 1000000 = full scale). bit_depth 8 is
// unsigned with a 128 midpoint; 16 is signed little-endian. Any other
// depth returns 0.
inline uint32_t peak_ppm(const uint8_t* data, size_t len, uint8_t bit_depth) {
  uint32_t peak = 0;
  if (bit_depth == 8) {
    for (size_t i = 0; i < len; ++i) {
      const uint32_t magnitude = static_cast<uint32_t>(std::abs(static_cast<int>(data[i]) - 128));
      if (magnitude > peak) peak = magnitude;
    }
    return static_cast<uint32_t>(static_cast<uint64_t>(peak) * 1000000u / 128u);
  }
  if (bit_depth == 16) {
    for (size_t i = 0; i + 1 < len; i += 2) {
      const int16_t sample = static_cast<int16_t>(static_cast<uint16_t>(data[i]) | (static_cast<uint16_t>(data[i + 1]) << 8));
      const uint32_t magnitude = static_cast<uint32_t>(std::abs(static_cast<int>(sample)));
      if (magnitude > peak) peak = magnitude;
    }
    return static_cast<uint32_t>(static_cast<uint64_t>(peak) * 1000000u / 32768u);
  }
  return 0;
}

}  // namespace nockvm::audio
