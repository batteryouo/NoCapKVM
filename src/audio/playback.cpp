#include "nockvm/audio/playback.h"
#include <algorithm>
#include <miniaudio.h>
#include "nockvm/telemetry/counters.h"

namespace nockvm::audio {
namespace {

// Audio packets arrive through the GUI loop, which normally runs every
// 16.7 ms with VSync. Eight 5 ms packets cover that cadence and ordinary
// scheduling variance before playback begins.
constexpr size_t kTargetDepth = 8;
// ~1 second of audio at the 5ms/packet the sender uses. Far more slack than
// any LAN jitter needs, so it never interferes with normal playback, while
// still capping what the buffer can hold at a couple hundred KB.
constexpr size_t kMaxDepth = 200;

ma_format to_ma_format(uint8_t bit_depth) { return bit_depth == 8 ? ma_format_u8 : ma_format_s16; }

// The context and device share a lifetime.
struct PlaybackImpl {
  ma_context context;
  ma_device device;
  JitterBuffer* buffer;  // not owned; AudioPlayback outlives the device
  uint8_t silence_byte;  // 0 for signed formats, 0x80 for unsigned (ma_format_u8) -- a plain
                         // zero-fill would be the loudest possible negative excursion, not silence, in u8
  // Whatever's left of the last packet popped. The device asks for however
  // many frames its own period happens to be, which has no reason to equal
  // the sender's packet size: the sender picks 5ms of audio, while the
  // backend picks whatever the hardware/shared-mode mixer gives it. Packets
  // therefore have to be treated as a byte stream spanning callbacks.
  std::vector<uint8_t> carry;
  size_t carry_pos = 0;
};

void data_callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frame_count) {
  auto* impl = static_cast<PlaybackImpl*>(device->pUserData);
  auto* out = static_cast<uint8_t*>(output);
  size_t remaining =
      static_cast<size_t>(frame_count) * ma_get_bytes_per_frame(device->playback.format, device->playback.channels);

  // Pull as many packets as this callback needs, keeping the remainder of
  // the last one for the next callback. The previous version popped exactly
  // one packet per callback and used it only if it happened to be at least
  // as big as the request -- so a request larger than a packet played
  // silence and threw the packet away (consuming one packet per callback
  // while the sender produced several), and a smaller one discarded the
  // rest of the packet. Either way the two rates were decoupled from the
  // actual audio, which is what let the buffer run away.
  while (remaining > 0) {
    if (impl->carry_pos >= impl->carry.size()) {
      auto frame = impl->buffer->pop();
      if (!frame) break;  // nothing available -- the rest of this buffer is silence
      impl->carry = std::move(*frame);
      impl->carry_pos = 0;
      if (impl->carry.empty()) continue;
    }
    const size_t n = std::min(remaining, impl->carry.size() - impl->carry_pos);
    std::copy(impl->carry.begin() + static_cast<std::ptrdiff_t>(impl->carry_pos),
              impl->carry.begin() + static_cast<std::ptrdiff_t>(impl->carry_pos + n), out);
    impl->carry_pos += n;
    out += n;
    remaining -= n;
  }
  if (remaining > 0) {
    std::fill(out, out + remaining, impl->silence_byte);
    // Real-time callback thread: atomic increment only, no I/O -- see
    // nockvm/telemetry/counters.h. The periodic reporter (main thread)
    // reads and reports this count, never this thread.
    telemetry::counters().audio_underruns.fetch_add(1, std::memory_order_relaxed);
  }
}

}  // namespace

AudioPlayback::AudioPlayback() : buffer_(std::make_unique<JitterBuffer>(kTargetDepth, kMaxDepth)) {}
AudioPlayback::~AudioPlayback() { stop(); }

bool AudioPlayback::start(const AudioFormat& format) {
  auto* impl = new PlaybackImpl();
  impl->buffer = buffer_.get();
  impl->silence_byte = format.bit_depth == 8 ? 0x80 : 0x00;

  if (ma_context_init(nullptr, 0, nullptr, &impl->context) != MA_SUCCESS) {
    delete impl;
    return false;
  }

  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = to_ma_format(format.bit_depth);
  config.playback.channels = kChannels;
  config.sampleRate = format.sample_rate;
  config.periodSizeInFrames = format.frame_count();
  config.dataCallback = data_callback;
  config.pUserData = impl;

  if (ma_device_init(&impl->context, &config, &impl->device) != MA_SUCCESS) {
    ma_context_uninit(&impl->context);
    delete impl;
    return false;
  }
  // A successfully initialized device must be uninitialized when start fails.
  if (ma_device_start(&impl->device) != MA_SUCCESS) {
    ma_device_uninit(&impl->device);
    ma_context_uninit(&impl->context);
    delete impl;
    return false;
  }

  device_ = impl;
  return true;
}

size_t AudioPlayback::buffered_packets() const { return buffer_->depth(); }
size_t AudioPlayback::buffer_capacity() const { return buffer_->capacity(); }
uint64_t AudioPlayback::buffer_cap_episodes() const { return buffer_->cap_episodes(); }

void AudioPlayback::stop() {
  if (!device_) return;
  auto* impl = static_cast<PlaybackImpl*>(device_);
  ma_device_uninit(&impl->device);
  ma_context_uninit(&impl->context);
  delete impl;
  device_ = nullptr;
}

void AudioPlayback::push_frame(uint32_t seq, std::vector<uint8_t> data) { buffer_->push(seq, std::move(data)); }

}  // namespace nockvm::audio
