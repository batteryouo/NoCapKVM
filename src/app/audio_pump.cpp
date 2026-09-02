#include "audio_pump.h"
#include "nockvm/audio/format.h"
#include "nockvm/audio/protocol.h"
#include "nockvm/discovery/audio_port_protocol.h"
#include "nockvm/discovery/connection_types.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

namespace nockvm::app {
namespace {

void stop_master_audio(AppState& state) {
  if (state.audio_playback) state.audio_playback->stop();
  state.audio_playback.reset();
  state.audio_recv_channel.reset();
}

void pump_master(AppState& state) {
  if (!state.tcp_server) {
    if (state.audio_active) {
      stop_master_audio(state);
      state.audio_active = false;
    }
    state.audio_master_control_sent = false;
    return;
  }

  const discovery::ConnectionInfo info = state.tcp_server->status();
  const bool tcp_connected = info.state == discovery::ConnectionState::Connected;

  if (!tcp_connected) {
    if (state.audio_active) {
      stop_master_audio(state);
      state.audio_active = false;
    }
    state.audio_master_control_sent = false;
    return;
  }

  // Push Master's desired settings to Slave -- once as soon as the
  // connection is up, and again on every subsequent change -- regardless
  // of whether Slave is currently sending, so the request is waiting for
  // Slave as soon as it looks.
  const bool want_send_control = !state.audio_master_control_sent ||
      state.audio_master_desired_send_enabled != state.audio_master_last_sent_enabled ||
      (state.audio_master_desired_send_enabled &&
       state.audio_master_desired_format != state.audio_master_last_sent_format);
  if (want_send_control) {
    const auto payload = discovery::encode_audio_settings(
        state.audio_master_desired_send_enabled, state.audio_master_desired_format.sample_rate,
        state.audio_master_desired_format.bit_depth);
    state.tcp_server->send_input(discovery::kMsgAudioControl, payload.data(), payload.size());
    state.audio_master_control_sent = true;
    state.audio_master_last_sent_enabled = state.audio_master_desired_send_enabled;
    state.audio_master_last_sent_format = state.audio_master_desired_format;
  }

  // peer_audio_send_enabled defaults to false until Slave reports
  // otherwise, so this doubles as "Slave hasn't said anything yet" and
  // "Slave explicitly disabled sending" -- both mean don't expect audio.
  const bool peer_sending = info.peer_audio_send_enabled;

  if (!peer_sending) {
    if (state.audio_active) {
      stop_master_audio(state);
      state.audio_active = false;
    }
    return;
  }

  const audio::AudioFormat peer_format{info.peer_audio_sample_rate, info.peer_audio_bit_depth};
  const bool format_changed = state.audio_active && peer_format != state.audio_master_active_format;

  if (!state.audio_active || format_changed) {
    // A format change means Slave itself just restarted capture (the user
    // changed the quality setting) -- restart playback to match, but the
    // recv channel/socket (which doesn't care about audio format at all,
    // it just moves bytes) doesn't need to be touched.
    if (state.audio_playback) state.audio_playback->stop();
    state.audio_playback = std::make_unique<audio::AudioPlayback>();
    state.audio_playback->start(peer_format);
    state.audio_master_active_format = peer_format;

    if (!state.audio_recv_channel) {
      state.audio_recv_channel = std::make_unique<audio::AudioChannel>(state.tcp_server->audio_socket(),
                                                                         audio::derive_audio_key(info.audio_key));
    }
    state.audio_active = true;
  }

  uint32_t seq = 0;
  std::vector<uint8_t> data;
  while (state.audio_recv_channel->receive_nonblocking(seq, data)) {
    state.audio_playback->push_frame(seq, std::move(data));
  }
}

// Order matters: stop the capture device first and only then tear down
// the channel/socket it sends through. AudioCapture::stop() blocks until
// any in-progress callback finishes (miniaudio's own contract), so this
// guarantees the capture callback can never touch a channel that's
// already been destroyed.
void stop_slave_audio(AppState& state) {
  if (state.audio_capture) state.audio_capture->stop();
  state.audio_capture.reset();
  state.audio_send_channel.reset();
  if (state.audio_send_socket != kInvalidSocket) {
    discovery::close_socket(state.audio_send_socket);
    state.audio_send_socket = kInvalidSocket;
  }
}

void pump_slave(AppState& state) {
  if (!state.tcp_client) {
    if (state.audio_active) {
      stop_slave_audio(state);
      state.audio_active = false;
    }
    state.audio_status_reported = false;
    return;
  }

  const discovery::ConnectionInfo info = state.tcp_client->status();
  const bool connected = info.state == discovery::ConnectionState::Connected;

  if (!connected) {
    if (state.audio_active) {
      stop_slave_audio(state);
      state.audio_active = false;
    }
    state.audio_status_reported = false;
    return;
  }

  // Apply Master's latest request, if it's new, before anything else --
  // edge-triggered (by sequence number) so an unchanged request doesn't
  // re-overwrite a local edit made in between.
  if (info.master_audio_control_received &&
      info.master_audio_control_seq != state.audio_last_applied_master_control_seq) {
    state.audio_send_enabled = info.master_requested_send_enabled;
    state.audio_desired_format.sample_rate = info.master_requested_sample_rate;
    state.audio_desired_format.bit_depth = info.master_requested_bit_depth;
    state.audio_last_applied_master_control_seq = info.master_audio_control_seq;
  }

  // Report the current actual settings to Master whenever they change --
  // including disabling sending entirely, so Master's UI never gets stuck
  // showing a stale format after Slave has actually stopped.
  const bool status_changed = !state.audio_status_reported ||
      state.audio_send_enabled != state.audio_last_reported_send_enabled ||
      (state.audio_send_enabled && state.audio_desired_format != state.audio_last_reported_format);
  if (status_changed) {
    const auto payload = discovery::encode_audio_settings(
        state.audio_send_enabled, state.audio_desired_format.sample_rate, state.audio_desired_format.bit_depth);
    state.tcp_client->send_message(discovery::kMsgAudioStatus, payload.data(), payload.size());
    state.audio_status_reported = true;
    state.audio_last_reported_send_enabled = state.audio_send_enabled;
    state.audio_last_reported_format = state.audio_desired_format;
  }

  if (!state.audio_send_enabled || info.peer_audio_port == 0) {
    if (state.audio_active) {
      stop_slave_audio(state);
      state.audio_active = false;
    }
    return;
  }

  const bool format_changed = state.audio_active && state.audio_active_format != state.audio_desired_format;
  if (state.audio_active && !format_changed) return;

  // Either starting fresh or the quality setting changed (from either
  // side) -- either way, capture needs to (re)start with
  // state.audio_desired_format. Master already heard about the new format
  // via the status report above.
  if (state.audio_capture) {
    state.audio_capture->stop();
    state.audio_capture.reset();
  }

  sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(info.peer_audio_port);
  inet_pton(AF_INET, info.peer_ip.c_str(), &dest.sin_addr);

  if (!state.audio_send_channel) {
    state.audio_send_socket = discovery::create_udp_socket();
    state.audio_send_channel =
        std::make_unique<audio::AudioChannel>(state.audio_send_socket, audio::derive_audio_key(info.audio_key));
  }

  audio::AudioChannel* channel = state.audio_send_channel.get();
  state.audio_capture = std::make_unique<audio::AudioCapture>();
  const bool started =
      state.audio_capture->start(state.audio_desired_format, [channel, dest](const uint8_t* data, size_t len) {
        channel->send_to(dest, data, len);
      });
  if (!started) {
    stop_slave_audio(state);
    state.audio_active = false;
    return;
  }

  state.audio_active_format = state.audio_desired_format;
  state.audio_active = true;
}

}  // namespace

void pump_audio(AppState& state) {
  if (state.role == discovery::Role::Master) {
    pump_master(state);
  } else {
    pump_slave(state);
  }
}

}  // namespace nockvm::app
