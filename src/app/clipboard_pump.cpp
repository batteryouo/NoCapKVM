#include "clipboard_pump.h"
#include "nockvm/clipboard/clipboard.h"
#include "nockvm/discovery/clipboard_protocol.h"
#include "nockvm/discovery/connection_types.h"

namespace nockvm::app {
namespace {

constexpr auto kPollInterval = std::chrono::seconds(1);

void apply_and_track(AppState& state, clipboard::ClipboardContent content) {
  clipboard::write_clipboard(content);
  state.clipboard_last_applied = content;
  state.clipboard_last_applied_valid = true;
  state.clipboard_last_seen = std::move(content);
  state.clipboard_last_seen_valid = true;
}

// Shared by both roles -- symmetric by design, see app_state.h's comment
// on the clipboard_* fields. `send` and `take_pending` paper over
// TcpServer/TcpClient's differently-named but otherwise identical methods
// (send_input/send_message, and each's own take_pending_clipboard).
template <typename SendFn, typename TakeFn>
void sync_clipboard(AppState& state, bool connected, SendFn&& send, TakeFn&& take_pending) {
  if (!connected) {
    state.clipboard_last_seen_valid = false;
    state.clipboard_last_applied_valid = false;
    return;
  }

  discovery::ClipboardMessage msg;
  if (take_pending(msg)) {
    const auto type = msg.type == discovery::ClipboardMsgType::Text ? clipboard::ContentType::Text
                                                                      : clipboard::ContentType::Jpeg;
    apply_and_track(state, clipboard::ClipboardContent{type, std::move(msg.data)});
  }

  const auto now = std::chrono::steady_clock::now();
  if (now - state.clipboard_last_check < kPollInterval) return;
  state.clipboard_last_check = now;

  std::optional<clipboard::ClipboardContent> current = clipboard::read_clipboard();
  if (!current) return;
  if (state.clipboard_last_seen_valid && *current == state.clipboard_last_seen) return;  // unchanged
  state.clipboard_last_seen = *current;
  state.clipboard_last_seen_valid = true;
  // Not a new local copy -- this is just the OS clipboard now reflecting
  // the content applied above (possibly on an earlier call, once the
  // throttle let this function actually look again). Sending it back
  // would ping-pong the same content forever.
  if (state.clipboard_last_applied_valid && *current == state.clipboard_last_applied) return;

  const uint8_t msg_type =
      current->type == clipboard::ContentType::Text ? discovery::kMsgClipboardText : discovery::kMsgClipboardImage;
  send(msg_type, current->data.data(), current->data.size());
}

}  // namespace

void pump_clipboard(AppState& state) {
  if (state.role == discovery::Role::Master) {
    if (!state.tcp_server) {
      state.clipboard_last_seen_valid = false;
      state.clipboard_last_applied_valid = false;
      return;
    }
    const bool connected = state.tcp_server->status().state == discovery::ConnectionState::Connected;
    sync_clipboard(
        state, connected,
        [&](uint8_t type, const uint8_t* data, size_t len) { state.tcp_server->send_input(type, data, len); },
        [&](discovery::ClipboardMessage& out) { return state.tcp_server->take_pending_clipboard(out); });
  } else {
    if (!state.tcp_client) {
      state.clipboard_last_seen_valid = false;
      state.clipboard_last_applied_valid = false;
      return;
    }
    const bool connected = state.tcp_client->status().state == discovery::ConnectionState::Connected;
    sync_clipboard(
        state, connected,
        [&](uint8_t type, const uint8_t* data, size_t len) { state.tcp_client->send_message(type, data, len); },
        [&](discovery::ClipboardMessage& out) { return state.tcp_client->take_pending_clipboard(out); });
  }
}

}  // namespace nockvm::app
