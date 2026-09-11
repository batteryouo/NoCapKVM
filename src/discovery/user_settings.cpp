#include "nockvm/discovery/user_settings.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include "config_dir.h"

namespace nockvm::discovery {
namespace {

constexpr int kMaxConnectionTimeoutSeconds = 3600;

std::filesystem::path store_path() { return config_dir() / "user_settings"; }

bool valid_format(const audio::AudioFormat& format) {
  return (format.sample_rate == 24000 || format.sample_rate == 48000) &&
         (format.bit_depth == 8 || format.bit_depth == 16);
}

void read_format(std::ifstream& in, audio::AudioFormat& format) {
  uint32_t sample_rate = 0;
  unsigned int bit_depth = 0;
  if (!(in >> sample_rate >> bit_depth)) return;
  const audio::AudioFormat candidate{sample_rate, static_cast<uint8_t>(bit_depth)};
  if (valid_format(candidate)) format = candidate;
}

}  // namespace

UserSettings load_user_settings() {
  UserSettings settings;
  std::ifstream in(store_path());
  std::string key;
  while (in >> key) {
    if (key == "connection_timeout_s") {
      int value = 0;
      if (in >> value && value >= 1 && value <= kMaxConnectionTimeoutSeconds) settings.connection_timeout_s = value;
    } else if (key == "auto_connect_enabled") {
      int value = 0;
      if (in >> value && (value == 0 || value == 1)) settings.auto_connect_enabled = value != 0;
    } else if (key == "slave_audio") {
      int enabled = 0;
      if (in >> enabled && (enabled == 0 || enabled == 1)) settings.slave_audio_send_enabled = enabled != 0;
      read_format(in, settings.slave_audio_format);
    } else if (key == "master_audio") {
      int overridden = 0;
      int enabled = 0;
      if (in >> overridden && (overridden == 0 || overridden == 1)) settings.master_audio_overridden = overridden != 0;
      if (in >> enabled && (enabled == 0 || enabled == 1)) settings.master_audio_send_enabled = enabled != 0;
      read_format(in, settings.master_audio_format);
    } else {
      std::string ignored;
      std::getline(in, ignored);
    }
  }
  return settings;
}

void save_user_settings(const UserSettings& settings) {
  std::error_code ec;
  std::filesystem::create_directories(config_dir(), ec);
  std::ofstream out(store_path());
  if (!out) return;
  out << "connection_timeout_s " << std::clamp(settings.connection_timeout_s, 1, kMaxConnectionTimeoutSeconds) << "\n";
  out << "auto_connect_enabled " << (settings.auto_connect_enabled ? 1 : 0) << "\n";
  out << "slave_audio " << (settings.slave_audio_send_enabled ? 1 : 0) << " "
      << settings.slave_audio_format.sample_rate << " " << static_cast<unsigned int>(settings.slave_audio_format.bit_depth) << "\n";
  out << "master_audio " << (settings.master_audio_overridden ? 1 : 0) << " "
      << (settings.master_audio_send_enabled ? 1 : 0) << " " << settings.master_audio_format.sample_rate << " "
      << static_cast<unsigned int>(settings.master_audio_format.bit_depth) << "\n";
}

}  // namespace nockvm::discovery
