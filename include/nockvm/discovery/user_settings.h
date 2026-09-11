#pragma once
#include <cstdint>
#include "nockvm/audio/format.h"

namespace nockvm::discovery {

// User choices that apply independently of the current connection.
struct UserSettings {
  int connection_timeout_s = 10;
  bool auto_connect_enabled = true;
  bool slave_audio_send_enabled = true;
  audio::AudioFormat slave_audio_format;
  bool master_audio_overridden = false;
  bool master_audio_send_enabled = true;
  audio::AudioFormat master_audio_format;
};

// Loads the settings stored in the app config directory. Missing, malformed,
// or unsupported values leave their corresponding defaults intact.
UserSettings load_user_settings();
void save_user_settings(const UserSettings& settings);

}  // namespace nockvm::discovery
