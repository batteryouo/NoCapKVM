#include "nockvm/audio/playback.h"
#include <algorithm>
#include <miniaudio.h>

namespace nockvm::audio {
namespace {

constexpr size_t kTargetDepth = 3;  // packets buffered before playback starts

ma_format to_ma_format(uint8_t bit_depth) { return bit_depth == 8 ? ma_format_u8 : ma_format_s16; }

// See capture.cpp's CaptureImpl comment: context and device must share the
// device's lifetime, not just start()'s.
struct PlaybackImpl {
  ma_context context;
  ma_device device;
  JitterBuffer* buffer;  // not owned; AudioPlayback outlives the device
  uint8_t silence_byte;  // 0 for signed formats, 0x80 for unsigned (ma_format_u8) -- a plain
                         // zero-fill would be the loudest possible negative excursion, not silence, in u8
};

void data_callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frame_count) {
  auto* impl = static_cast<PlaybackImpl*>(device->pUserData);
  auto* out = static_cast<uint8_t*>(output);
  const size_t needed_bytes =
      static_cast<size_t>(frame_count) * ma_get_bytes_per_frame(device->playback.format, device->playback.channels);

  const auto frame = impl->buffer->pop();
  if (frame && frame->size() >= needed_bytes) {
    std::copy(frame->begin(), frame->begin() + needed_bytes, out);
  } else {
    std::fill(out, out + needed_bytes, impl->silence_byte);
  }
}

}  // namespace

AudioPlayback::AudioPlayback() : buffer_(std::make_unique<JitterBuffer>(kTargetDepth)) {}
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

  if (ma_device_init(&impl->context, &config, &impl->device) != MA_SUCCESS ||
      ma_device_start(&impl->device) != MA_SUCCESS) {
    ma_context_uninit(&impl->context);
    delete impl;
    return false;
  }

  device_ = impl;
  return true;
}

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
