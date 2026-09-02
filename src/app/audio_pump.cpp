#include "audio_pump.h"
#include "nockvm/audio/format.h"
#include "nockvm/audio/protocol.h"
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
    return;
  }

  const discovery::ConnectionInfo info = state.tcp_server->status();
  const bool connected = info.state == discovery::ConnectionState::Connected;

  if (!connected) {
    if (state.audio_active) {
      stop_master_audio(state);
      state.audio_active = false;
    }
    return;
  }

  if (!state.audio_active) {
    state.audio_playback = std::make_unique<audio::AudioPlayback>();
    state.audio_playback->start();
    state.audio_recv_channel =
        std::make_unique<audio::AudioChannel>(state.tcp_server->audio_socket(), audio::derive_audio_key(info.audio_key));
    state.audio_active = true;
  }

  uint32_t seq = 0;
  std::vector<int16_t> samples;
  while (state.audio_recv_channel->receive_nonblocking(seq, samples)) {
    state.audio_playback->push_frame(seq, std::move(samples));
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
    return;
  }

  const discovery::ConnectionInfo info = state.tcp_client->status();
  const bool connected = info.state == discovery::ConnectionState::Connected && info.peer_audio_port != 0;

  if (!connected) {
    if (state.audio_active) {
      stop_slave_audio(state);
      state.audio_active = false;
    }
    return;
  }

  if (!state.audio_active) {
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(info.peer_audio_port);
    inet_pton(AF_INET, info.peer_ip.c_str(), &dest.sin_addr);

    state.audio_send_socket = discovery::create_udp_socket();
    state.audio_send_channel =
        std::make_unique<audio::AudioChannel>(state.audio_send_socket, audio::derive_audio_key(info.audio_key));

    audio::AudioChannel* channel = state.audio_send_channel.get();
    state.audio_capture = std::make_unique<audio::AudioCapture>();
    const bool started = state.audio_capture->start([channel, dest](const int16_t* samples, unsigned int frame_count) {
      channel->send_to(dest, samples, frame_count * audio::kChannels);
    });
    if (!started) {
      stop_slave_audio(state);
      return;
    }
    state.audio_active = true;
  }
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
