#pragma once
#include <string>
#include <miniaudio.h>

namespace nockvm::audio {

namespace detail {

// ma_device's capture and playback members are distinct anonymous structs
// with the same field names.
template <typename Side>
std::string describe_side(const ma_context& context, const ma_device& device, const Side& side) {
  // Sized from a length query: ma_channel_map_to_string() leaves the buffer
  // unterminated when it is too small, and it only terminates when
  // bufferCap > len + 1, hence the extra two bytes.
  const size_t map_len = ma_channel_map_to_string(side.internalChannelMap, side.internalChannels, nullptr, 0);
  std::string channel_map(map_len + 2, '\0');
  ma_channel_map_to_string(side.internalChannelMap, side.internalChannels, channel_map.data(), channel_map.size());
  channel_map.resize(map_len);
  std::string out = "backend=";
  out += ma_get_backend_name(context.backend);
  out += " device=\"";
  out += side.name;
  out += "\" requested=";
  out += ma_get_format_name(side.format);
  out += "/" + std::to_string(side.channels) + "ch/" + std::to_string(device.sampleRate) + "hz internal=";
  out += ma_get_format_name(side.internalFormat);
  out += "/" + std::to_string(side.internalChannels) + "ch/" + std::to_string(side.internalSampleRate) + "hz";
  out += " channel_map=";
  out += channel_map;
  out += " period_frames=" + std::to_string(side.internalPeriodSizeInFrames);
  return out;
}

}  // namespace detail

// One-line summary of what an initialized device actually negotiated: the
// backend, the device name, and the format/channels/rate/channel map on the
// device side ("internal") versus what this app requested. miniaudio
// converts between the two, so a mismatch here is not an error by itself,
// but it is what to look at when the audio arrives silent or garbled.
inline std::string describe_device(const ma_context& context, const ma_device& device) {
  if (device.type == ma_device_type_capture || device.type == ma_device_type_loopback) {
    return detail::describe_side(context, device, device.capture);
  }
  return detail::describe_side(context, device, device.playback);
}

}  // namespace nockvm::audio
