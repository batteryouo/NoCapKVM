#pragma once
#include <cstdint>
#include <functional>

namespace nockvm::audio {

// Captures this machine's own system audio output (loopback) and invokes
// on_frame with each chunk of interleaved 16-bit PCM at kSampleRate/
// kChannels (format.h). On Windows this is WASAPI's native loopback mode;
// on Linux there's no equivalent flag, so start() enumerates devices and
// picks the capture device matching the current default sink's monitor
// (the PulseAudio/PipeWire convention) -- unverified against a real
// desktop environment until tested there.
class AudioCapture {
public:
  AudioCapture();
  ~AudioCapture();
  AudioCapture(const AudioCapture&) = delete;
  AudioCapture& operator=(const AudioCapture&) = delete;

  bool start(std::function<void(const int16_t* samples, unsigned int frame_count)> on_frame);
  void stop();

private:
  void* device_ = nullptr;  // ma_device*, opaque here to keep miniaudio out of this header
};

}  // namespace nockvm::audio
