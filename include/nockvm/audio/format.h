#pragma once
#include <cstddef>
#include <cstdint>

namespace nockvm::audio {

// Fixed for v1 -- no format negotiation, no codec. miniaudio's own
// resampler/converter handles any mismatch with a real device's native
// format on either end.
constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;

// 5ms per packet (240 frames @ 48kHz) keeps each UDP packet's plaintext
// well under a typical 1500-byte MTU (240 * 2 channels * 2 bytes = 960
// bytes, + 4-byte seq + 16-byte AEAD tag = 980 bytes) with room to spare,
// while staying small enough not to add noticeable latency on its own.
constexpr uint32_t kFrameCount = 240;
constexpr size_t kSamplesPerPacket = kFrameCount * kChannels;

}  // namespace nockvm::audio
