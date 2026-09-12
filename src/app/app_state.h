#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include "nockvm/audio/capture.h"
#include "nockvm/audio/channel.h"
#include "nockvm/audio/playback.h"
#include "nockvm/clipboard/clipboard.h"
#include "nockvm/discovery/announcer.h"
#include "nockvm/discovery/known_peers.h"
#include "nockvm/discovery/listener.h"
#include "nockvm/discovery/platform_socket.h"
#include "nockvm/discovery/tcp_client.h"
#include "nockvm/discovery/tcp_server.h"
#include "nockvm/discovery/types.h"
#include "nockvm/display/monitor_info.h"
#include "nockvm/input/hook.h"
#include "nockvm/topology/arrangement.h"
#include "audio_loss_window.h"
#include "buffer_depth_window.h"
#include "retry_backoff.h"

namespace nockvm::app {

enum class Screen { RoleSelect, Discovery, ManageDevices, Arrangement };

// A currently held key, including the data needed to release it remotely.
struct HeldKey {
  uint32_t vk = 0;
  uint32_t scancode = 0;
  bool extended = false;
};

struct AppState {
  Screen screen = Screen::RoleSelect;
  Screen previous_screen = Screen::RoleSelect;  // Destination of Back from sub-screens.
  discovery::Role role = discovery::Role::Master;
  uint64_t device_id = 0;
  std::string hostname;
  std::vector<display::MonitorInfo> local_monitors;
  discovery::KnownPeers known_peers;
  topology::ScreenArrangement screen_arrangement;
  std::unique_ptr<discovery::Announcer> announcer;
  std::unique_ptr<discovery::Listener> listener;
  std::unique_ptr<discovery::TcpServer> tcp_server;  // Master only
  std::unique_ptr<discovery::TcpClient> tcp_client;  // Slave only.
  // Maximum reconnect or idle-connection duration.
  int connection_timeout_s = 10;
  // Enables automatic connections to discovered Masters.
  bool auto_connect_enabled = true;
  // Masters not eligible for automatic reconnection in this session.
  std::unordered_set<uint64_t> auto_connect_suppressed;

  // Master-side input capture and handoff state.
  input::InputHook input_hook;
  bool input_hook_active = false;
  bool input_owned_by_master = true;
  int32_t input_logical_x = 0, input_logical_y = 0;
  // Suppresses boundary detection for the frame after a handoff.
  bool input_just_handed_off = false;
  // Keys held on the current input owner.
  std::vector<HeldKey> input_held_keys;

  // Slave-to-Master audio state.
  bool audio_active = false;
  std::unique_ptr<audio::AudioPlayback> audio_playback;      // Master only
  // Master only: incremented each time audio_playback is (re)constructed,
  // so telemetry can tell a fresh instance's counters (which restart at 0)
  // apart from the previous instance's.
  uint64_t audio_playback_generation = 0;
  // Master only: snapshot of telemetry::counters().audio_underruns taken
  // whenever audio_playback is (re)constructed, so the Connection Quality
  // panel can show underruns scoped to the currently active playback
  // instance instead of the whole process lifetime (that counter itself is
  // process-wide and never resets). Note this resets on every
  // (re)construction -- including a mid-connection audio-quality change,
  // not just a fresh TCP connection -- which is why the UI labels it
  // "since playback started" rather than "this connection".
  uint64_t audio_underruns_baseline = 0;
  // Master only: rolling ~60-second window over playout_misses()/
  // frames_played(), sampled once per second in audio_pump.cpp's
  // pump_master() -- see AudioLossWindow's own comment. Reset alongside
  // audio_underruns_baseline above, for the same reason.
  AudioLossWindow audio_loss_window;
  std::chrono::steady_clock::time_point audio_loss_window_last_sample_at{};
  BufferDepthWindow jitter_buffer_depth_window;
  std::unique_ptr<audio::AudioChannel> audio_recv_channel;   // Master only, wraps tcp_server's audio_socket()
  audio::AudioFormat audio_master_active_format;  // Master only: what audio_playback is currently configured for
  std::unique_ptr<audio::AudioCapture> audio_capture;        // Slave only
  std::unique_ptr<audio::AudioChannel> audio_send_channel;   // Slave only
  socket_t audio_send_socket = kInvalidSocket;                // Slave only; AudioChannel doesn't own the socket
  audio::AudioFormat audio_active_format;  // Slave only: what audio_capture is currently running with
  // Slave-only: gates AudioCapture::start() retries after a failure. See retry_backoff.h.
  RetryBackoff audio_slave_start_backoff{std::chrono::seconds(2)};

  // Slave-side capture settings requested by the local UI or Master.
  bool audio_send_enabled = true;
  audio::AudioFormat audio_desired_format;

  // Master-side requested capture settings.
  bool audio_master_desired_send_enabled = true;
  audio::AudioFormat audio_master_desired_format;
  // False while Master mirrors the settings reported by Slave.
  bool audio_master_overridden = false;
  bool audio_master_control_sent = false;  // at least one kMsgAudioControl sent this connection
  bool audio_master_last_sent_enabled = true;
  audio::AudioFormat audio_master_last_sent_format;

  // Slave-side message and status synchronization state.
  uint32_t audio_last_applied_master_control_seq = 0;
  bool audio_status_reported = false;  // at least one kMsgAudioStatus sent this connection
  bool audio_last_reported_send_enabled = true;
  audio::AudioFormat audio_last_reported_format;

  // Bidirectional clipboard synchronization state.
  bool clipboard_last_seen_valid = false;
  clipboard::ClipboardContent clipboard_last_seen;
  // Last content written locally from a peer, used to suppress echoes.
  bool clipboard_last_applied_valid = false;
  clipboard::ClipboardContent clipboard_last_applied;
  // Time of the most recent clipboard poll.
  std::chrono::steady_clock::time_point clipboard_last_check{};

  // Telemetry, driven once per frame by pump_telemetry() in
  // telemetry_pump.cpp. Tracks the last-logged window visibility/
  // connection state purely to edge-trigger transition log lines -- the
  // actual counters live in nockvm::telemetry::counters(), not here.
  bool telemetry_last_window_visible = true;
  discovery::ConnectionState telemetry_last_connection_state = discovery::ConnectionState::Idle;

  // Connection Quality panel state (see reconnect_tracker.h): whether this
  // process has completed at least one successful connection yet, and how
  // many times it has reached Connected again since. Both persist across
  // disconnects for the life of the process, reset only on app restart.
  bool ever_connected = false;
  uint32_t reconnect_count = 0;
};

}  // namespace nockvm::app
