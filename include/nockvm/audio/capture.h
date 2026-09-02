#pragma once
#include <cstdint>
#include <functional>
#include "nockvm/audio/format.h"

namespace nockvm::audio {

// Captures this machine's own system audio output (loopback) in the given
// format and invokes on_frame with each chunk of raw interleaved PCM
// bytes. On Windows this is WASAPI's native loopback mode; on Linux
// there's no equivalent flag, so start() enumerates devices and picks the
// capture device matching the current default sink's monitor (the
// PulseAudio/PipeWire convention) -- unverified against a real desktop
// environment until tested there.
class AudioCapture {
public:
  AudioCapture();
  ~AudioCapture();
  AudioCapture(const AudioCapture&) = delete;
  AudioCapture& operator=(const AudioCapture&) = delete;

  bool start(const AudioFormat& format, std::function<void(const uint8_t* data, size_t len)> on_frame);
  void stop();

private:
  void* device_ = nullptr;  // ma_device*, opaque here to keep miniaudio out of this header
};

}  // namespace nockvm::audio
