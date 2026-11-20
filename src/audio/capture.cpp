#include "nockvm/audio/capture.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <miniaudio.h>
#include "device_info.h"
#include "nockvm/audio/level.h"
#include "nockvm/audio/packet_chunker.h"
#include "nockvm/telemetry/counters.h"

namespace nockvm::audio {
namespace {

// The context and device share a lifetime.
struct CaptureImpl {
  explicit CaptureImpl(size_t packet_bytes) : chunker(packet_bytes) {}
  ma_context context{};
  ma_device device{};
  std::function<void(const uint8_t*, size_t)> on_frame;
  PacketChunker chunker;  // touched only from the device's data callback
};

ma_format to_ma_format(uint8_t bit_depth) { return bit_depth == 8 ? ma_format_u8 : ma_format_s16; }

void data_callback(ma_device* device, void* /*output*/, const void* input, ma_uint32 frame_count) {
  auto* impl = static_cast<CaptureImpl*>(device->pUserData);
  const size_t bytes = static_cast<size_t>(frame_count) * ma_get_bytes_per_frame(device->capture.format, device->capture.channels);
  const uint8_t bit_depth = device->capture.format == ma_format_u8 ? 8 : 16;
  telemetry::record_peak(telemetry::counters().audio_capture_peak_ppm,
                         peak_ppm(static_cast<const uint8_t*>(input), bytes, bit_depth));
  impl->chunker.feed(static_cast<const uint8_t*>(input), bytes,
                     [impl](const uint8_t* packet, size_t len) { impl->on_frame(packet, len); });
}

#ifdef _WIN32
constexpr ma_device_type kDeviceType = ma_device_type_loopback;
bool select_device_id(ma_context&, ma_device_id&) { return false; }  // unused; loopback needs no explicit device
#else
constexpr ma_device_type kDeviceType = ma_device_type_capture;

// Prefer a PulseAudio or PipeWire monitor source for output capture.
bool select_device_id(ma_context& context, ma_device_id& out_id) {
  ma_device_info* capture_infos = nullptr;
  ma_uint32 capture_count = 0;
  if (ma_context_get_devices(&context, nullptr, nullptr, &capture_infos, &capture_count) != MA_SUCCESS) return false;

  for (ma_uint32 i = 0; i < capture_count; ++i) {
    std::string name = capture_infos[i].name;
    std::transform(name.begin(), name.end(), name.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name.find("monitor") != std::string::npos) {
      out_id = capture_infos[i].id;
      return true;
    }
  }
  return false;
}
#endif

}  // namespace

AudioCapture::AudioCapture() = default;
AudioCapture::~AudioCapture() { stop(); }

bool AudioCapture::start(const AudioFormat& format, std::function<void(const uint8_t*, size_t)> on_frame) {
  if (!format.is_supported()) return false;
  auto* impl = new CaptureImpl(format.bytes_per_packet());
  impl->on_frame = std::move(on_frame);

  if (ma_context_init(nullptr, 0, nullptr, &impl->context) != MA_SUCCESS) {
    delete impl;
    return false;
  }

  ma_device_config config = ma_device_config_init(kDeviceType);
  config.capture.format = to_ma_format(format.bit_depth);
  config.capture.channels = kChannels;
  config.sampleRate = format.sample_rate;
  config.periodSizeInFrames = format.frame_count();
  config.dataCallback = data_callback;
  config.pUserData = impl;

  ma_device_id device_id{};
  if (select_device_id(impl->context, device_id)) config.capture.pDeviceID = &device_id;

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

  description_ = describe_device(impl->context, impl->device);
  device_ = impl;
  return true;
}

void AudioCapture::stop() {
  if (!device_) return;
  auto* impl = static_cast<CaptureImpl*>(device_);
  ma_device_uninit(&impl->device);
  ma_context_uninit(&impl->context);
  delete impl;
  device_ = nullptr;
}

}  // namespace nockvm::audio
