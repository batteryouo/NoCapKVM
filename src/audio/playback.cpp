#include "nockvm/audio/playback.h"
#include <algorithm>
#include "nockvm/audio/format.h"
#include <miniaudio.h>

namespace nockvm::audio {
namespace {

constexpr size_t kTargetDepth = 3;  // packets buffered before playback starts

// See capture.cpp's CaptureImpl comment: context and device must share the
// device's lifetime, not just start()'s.
struct PlaybackImpl {
  ma_context context;
  ma_device device;
  JitterBuffer* buffer;  // not owned; AudioPlayback outlives the device
};

void data_callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frame_count) {
  auto* impl = static_cast<PlaybackImpl*>(device->pUserData);
  auto* out = static_cast<int16_t*>(output);
  const auto frame = impl->buffer->pop();
  if (frame && frame->size() >= static_cast<size_t>(frame_count) * kChannels) {
    std::copy(frame->begin(), frame->begin() + frame_count * kChannels, out);
  } else {
    std::fill(out, out + frame_count * kChannels, static_cast<int16_t>(0));
  }
}

}  // namespace

AudioPlayback::AudioPlayback() : buffer_(std::make_unique<JitterBuffer>(kTargetDepth)) {}
AudioPlayback::~AudioPlayback() { stop(); }

bool AudioPlayback::start() {
  auto* impl = new PlaybackImpl();
  impl->buffer = buffer_.get();

  if (ma_context_init(nullptr, 0, nullptr, &impl->context) != MA_SUCCESS) {
    delete impl;
    return false;
  }

  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_s16;
  config.playback.channels = kChannels;
  config.sampleRate = kSampleRate;
  config.periodSizeInFrames = kFrameCount;
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

void AudioPlayback::push_frame(uint32_t seq, std::vector<int16_t> frame) { buffer_->push(seq, std::move(frame)); }

}  // namespace nockvm::audio
