#pragma once
#include <cstddef>
#include <cstdint>

namespace nockvm::audio {

// Fixed for v1: stereo only. Sample rate and bit depth are user-selectable
// (Discovery screen's Audio tab) since they're the two effective
// bandwidth/quality levers; channel count isn't worth the extra UI or
// protocol complexity for a personal tool.
constexpr uint32_t kChannels = 2;

struct AudioFormat {
  uint32_t sample_rate = 48000;
  uint8_t bit_depth = 16;  // 8 or 16

  uint32_t bytes_per_sample() const { return bit_depth / 8; }
  uint32_t bytes_per_frame() const { return bytes_per_sample() * kChannels; }

  // ~5ms of audio per packet regardless of format -- keeps every
  // combination well under a typical 1500-byte MTU (worst case, 48kHz/
  // 16-bit: 240 frames * 4 bytes/frame = 960 bytes, + 4-byte seq + 16-byte
  // AEAD tag = 980 bytes) while staying small enough not to add
  // noticeable latency on its own.
  uint32_t frame_count() const { return sample_rate / 200; }
  size_t bytes_per_packet() const { return static_cast<size_t>(frame_count()) * bytes_per_frame(); }

  bool operator==(const AudioFormat& other) const {
    return sample_rate == other.sample_rate && bit_depth == other.bit_depth;
  }
  bool operator!=(const AudioFormat& other) const { return !(*this == other); }
};

}  // namespace nockvm::audio
