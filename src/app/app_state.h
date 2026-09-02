#pragma once
#include <memory>
#include <string>
#include <vector>
#include "nockvm/audio/capture.h"
#include "nockvm/audio/channel.h"
#include "nockvm/audio/playback.h"
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

namespace nockvm::app {

enum class Screen { RoleSelect, Discovery, ManageDevices, Arrangement };

struct AppState {
  Screen screen = Screen::RoleSelect;
  Screen previous_screen = Screen::RoleSelect;  // where "Back" on ManageDevices/Arrangement returns to
  discovery::Role role = discovery::Role::Master;
  uint64_t device_id = 0;
  std::string hostname;
  std::vector<display::MonitorInfo> local_monitors;  // this machine's own displays, queried once at startup
  discovery::KnownPeers known_peers;  // loaded once at startup, shared by reference
  topology::ScreenArrangement screen_arrangement;  // Master-only: where each known peer's cluster sits, loaded once
  std::unique_ptr<discovery::Announcer> announcer;
  std::unique_ptr<discovery::Listener> listener;
  std::unique_ptr<discovery::TcpServer> tcp_server;  // Master only
  std::unique_ptr<discovery::TcpClient> tcp_client;  // Slave only, set on Connect click
  // Slave-only, UI-controlled (Discovery screen's Connection tab): how long
  // TcpClient's auto-reconnect loop keeps retrying a continuous stretch of
  // disconnection before giving up, so it doesn't retry forever in the
  // background once the Master is genuinely gone.
  int reconnect_timeout_s = 10;

  // Master-only input capture/handoff state (brief §3.2), driven once per
  // frame by pump_input() in input_pump.cpp.
  input::InputHook input_hook;
  bool input_hook_active = false;    // whether install() has been called (tracks the Connected transition)
  bool input_owned_by_master = true;
  int32_t input_logical_x = 0, input_logical_y = 0;  // current owner's own local coordinate space
  // A crossing's landing point sits exactly on the shared edge, so the
  // frame right after a handoff is already touching the boundary that
  // would trigger crossing back the other way. Set on every handoff (both
  // directions), consumed (and cleared) by the very next frame's crossing
  // check so a stray post-handoff jitter can't immediately bounce it back.
  bool input_just_handed_off = false;
  std::vector<uint32_t> input_held_vks;  // currently-held virtual-key codes, for the on-screen key monitor

  // Audio routing, Slave -> Master (brief §3.3), driven once per frame by
  // pump_audio() in audio_pump.cpp. Only the pair matching this machine's
  // current role is ever populated.
  bool audio_active = false;  // tracks the Connected transition, same idea as input_hook_active
  std::unique_ptr<audio::AudioPlayback> audio_playback;      // Master only
  std::unique_ptr<audio::AudioChannel> audio_recv_channel;   // Master only, wraps tcp_server's audio_socket()
  audio::AudioFormat audio_master_active_format;  // Master only: what audio_playback is currently configured for
  std::unique_ptr<audio::AudioCapture> audio_capture;        // Slave only
  std::unique_ptr<audio::AudioChannel> audio_send_channel;   // Slave only
  socket_t audio_send_socket = kInvalidSocket;                // Slave only; AudioChannel doesn't own the socket
  audio::AudioFormat audio_active_format;  // Slave only: what audio_capture is currently running with

  // Slave-only, UI-controlled (Discovery screen's Audio tab): whether to
  // capture/send at all, and at what quality. pump_audio() compares these
  // against audio_active_format/audio_active each frame and restarts
  // capture whenever they differ from what's currently running. Editable
  // either by the user directly on Slave, or overwritten by pump_audio()
  // when a new request arrives from Master (see audio_last_applied_
  // master_control_seq below) -- either way, the same fields drive
  // capture, so both paths are handled by one code path.
  bool audio_send_enabled = true;
  audio::AudioFormat audio_desired_format;

  // Bidirectional audio settings sync: either machine's Audio tab can now
  // edit these settings, and whichever side changes them announces the
  // change to the other over the existing SecureChannel (kMsgAudioStatus
  // Slave->Master, kMsgAudioControl Master->Slave) so both UIs and the
  // actual capture/playback stay consistent regardless of which side the
  // user touches.

  // Master-only, UI-controlled: what Master wants Slave's capture set to.
  // pump_audio() sends this to Slave once per connection and again on
  // every subsequent change (tracked via audio_master_last_sent_*).
  bool audio_master_desired_send_enabled = true;
  audio::AudioFormat audio_master_desired_format;
  bool audio_master_control_sent = false;  // at least one kMsgAudioControl sent this connection
  bool audio_master_last_sent_enabled = true;
  audio::AudioFormat audio_master_last_sent_format;

  // Slave-only: tracks the last kMsgAudioControl actually applied from
  // Master (by sequence number, so a request is applied exactly once
  // rather than every frame, which would otherwise fight a local edit
  // made in between) and the last status Slave itself reported to Master
  // (so a local change, including disabling sending, is announced exactly
  // once rather than repeated every frame).
  uint32_t audio_last_applied_master_control_seq = 0;
  bool audio_status_reported = false;  // at least one kMsgAudioStatus sent this connection
  bool audio_last_reported_send_enabled = true;
  audio::AudioFormat audio_last_reported_format;
};

}  // namespace nockvm::app
