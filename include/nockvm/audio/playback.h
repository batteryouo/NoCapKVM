#pragma once
#include <cstdint>
#include <memory>
#include "nockvm/audio/format.h"
#include "nockvm/audio/jitter_buffer.h"

namespace nockvm::audio {

// Plays back audio fed in via push_frame() (called from whatever thread is
// receiving network packets), through this machine's default output
// device, in the given format -- which must match whatever format the
// remote side's AudioCapture is actually using (communicated out of band
// by the discovery layer's kMsgAudioStatus/kMsgAudioControl; this class has no way to
// detect a mismatch on its own, it'll just play garbled audio). Internally
// paced by a JitterBuffer so out-of-order/lost network packets become
// silence rather than blocking playback.
class AudioPlayback {
public:
  AudioPlayback();
  ~AudioPlayback();
  AudioPlayback(const AudioPlayback&) = delete;
  AudioPlayback& operator=(const AudioPlayback&) = delete;

  bool start(const AudioFormat& format);
  void stop();

  void push_frame(uint32_t seq, std::vector<uint8_t> data);

  // Diagnostic, surfaced in Master's UI: see JitterBuffer::depth().
  size_t buffered_packets() const;
  // The ceiling buffered_packets() is measured against -- see JitterBuffer::capacity().
  size_t buffer_capacity() const;
  // See JitterBuffer::cap_episodes().
  uint64_t buffer_cap_episodes() const;
  // Basis for the UI's "audio playout misses" metric -- see
  // JitterBuffer::playout_misses(). An estimate: it can represent packet
  // loss, late arrival, or a receive stall, not exact network loss.
  uint64_t playout_misses() const;
  // See JitterBuffer::frames_played() -- the denominator playout_misses()
  // is naturally measured against.
  uint64_t frames_played() const;

private:
  void* device_ = nullptr;  // ma_device*, opaque here to keep miniaudio out of this header
  std::unique_ptr<JitterBuffer> buffer_;
};

}  // namespace nockvm::audio
