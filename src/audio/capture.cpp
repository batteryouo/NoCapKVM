#include "nockvm/audio/capture.h"
#include <algorithm>
#include <cctype>
#include <string>
#include "nockvm/audio/format.h"
#include <miniaudio.h>

namespace nockvm::audio {
namespace {

// Bundled into one heap allocation: ma_device keeps using its ma_context
// for as long as it's running, so the context can't be a local that goes
// out of scope when start() returns -- both need to live exactly as long
// as the device does.
struct CaptureImpl {
  ma_context context;
  ma_device device;
  std::function<void(const int16_t*, unsigned int)> on_frame;
};

void data_callback(ma_device* device, void* /*output*/, const void* input, ma_uint32 frame_count) {
  auto* impl = static_cast<CaptureImpl*>(device->pUserData);
  impl->on_frame(static_cast<const int16_t*>(input), frame_count);
}

#ifdef _WIN32
constexpr ma_device_type kDeviceType = ma_device_type_loopback;
bool select_device_id(ma_context&, ma_device_id&) { return false; }  // unused; loopback needs no explicit device
#else
constexpr ma_device_type kDeviceType = ma_device_type_capture;

// No loopback flag exists outside WASAPI -- find the capture device whose
// name looks like a PulseAudio/PipeWire "monitor" source (their convention
// for "this is actually a sink's output, exposed as a capture source").
// Unverified against a real desktop environment: the exact name miniaudio
// surfaces for this depends on the PulseAudio/PipeWire backend and the
// system's own device naming, both out of this project's control.
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
  return false;  // nothing matched -- caller falls back to the default device
}
#endif

}  // namespace

AudioCapture::AudioCapture() = default;
AudioCapture::~AudioCapture() { stop(); }

bool AudioCapture::start(std::function<void(const int16_t*, unsigned int)> on_frame) {
  auto* impl = new CaptureImpl();
  impl->on_frame = std::move(on_frame);

  if (ma_context_init(nullptr, 0, nullptr, &impl->context) != MA_SUCCESS) {
    delete impl;
    return false;
  }

  ma_device_config config = ma_device_config_init(kDeviceType);
  config.capture.format = ma_format_s16;
  config.capture.channels = kChannels;
  config.sampleRate = kSampleRate;
  config.periodSizeInFrames = kFrameCount;
  config.dataCallback = data_callback;
  config.pUserData = impl;

  ma_device_id device_id{};
  if (select_device_id(impl->context, device_id)) config.capture.pDeviceID = &device_id;

  if (ma_device_init(&impl->context, &config, &impl->device) != MA_SUCCESS ||
      ma_device_start(&impl->device) != MA_SUCCESS) {
    ma_context_uninit(&impl->context);
    delete impl;
    return false;
  }

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
