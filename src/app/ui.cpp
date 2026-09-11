#include "ui.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <imgui.h>
#include "nockvm/discovery/connection_types.h"
#include "nockvm/discovery/identity.h"
#include "nockvm/discovery/types.h"
#include "nockvm/discovery/user_settings.h"
#include "nockvm/telemetry/counters.h"
#include "nockvm/topology/crossing.h"
#include "quit.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#endif

namespace nockvm::app {
namespace {

// This process's resident memory, in MiB. Shown next to the audio jitter
// depth on Master's connection panel: between them they distinguish "the
// machine got slow because this process is growing" from "it got slow for
// some other reason", which is otherwise only guessable after the fact.
double resident_mib() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS pmc{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0.0;
  return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
#else
  std::FILE* f = std::fopen("/proc/self/statm", "r");
  if (!f) return 0.0;
  long total_pages = 0, resident_pages = 0;
  const int matched = std::fscanf(f, "%ld %ld", &total_pages, &resident_pages);
  std::fclose(f);
  if (matched != 2) return 0.0;
  return static_cast<double>(resident_pages) * static_cast<double>(sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
#endif
}

// Makes the ImGui window fill the whole GLFW window instead of floating as
// an independently movable/auto-sizing panel inside it — the previous
// AlwaysAutoResize|NoResize style just grows to fit content with no
// scrollbar, so content bigger than the OS window (e.g. the arrangement
// canvas) became unreachable without resizing the OS window itself.
//
// Deliberately NOT using ImGuiWindowFlags_NoDecoration: that's a bundle
// that also sets NoScrollbar, which would silently remove the window's
// own scrollbars — if the page's content (any screen, not just the
// canvas) is taller/wider than the OS window, there'd be no way to reach
// the rest of it. HorizontalScrollbar is requested explicitly since it
// isn't on by default; the vertical scrollbar is ImGui's default behavior
// whenever content overflows and isn't suppressed here.
void begin_fullscreen_window() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("NoCapKVM", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_HorizontalScrollbar);

  // Present on every screen: now that the OS close button just hides the
  // window to the tray instead of quitting (see tray.h), this is the app's
  // only in-UI way to actually end the process.
  constexpr float kExitWidth = 60.0f;
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - kExitWidth - 12.0f);
  if (ImGui::Button("Exit", ImVec2(kExitWidth, 0))) nockvm::app::request_quit();
}

void start_discovery(AppState& state, discovery::Role role) {
  state.role = role;
  discovery::save_last_role(role);

  uint16_t tcp_port = 0;
  if (role == discovery::Role::Master) {
    state.tcp_server = std::make_unique<discovery::TcpServer>(state.device_id, state.known_peers,
                                                                 std::chrono::seconds(state.connection_timeout_s));
    state.tcp_server->start();
    tcp_port = state.tcp_server->port();
  }

  state.announcer = std::make_unique<discovery::Announcer>(state.device_id, role, tcp_port, state.hostname);
  state.listener = std::make_unique<discovery::Listener>(state.device_id);
  state.announcer->start();
  state.listener->start();
  state.screen = Screen::Discovery;
}

const char* role_label(discovery::Role role) { return role == discovery::Role::Master ? "Master" : "Slave"; }

void save_user_settings(const AppState& state) {
  discovery::UserSettings settings;
  settings.connection_timeout_s = state.connection_timeout_s;
  settings.auto_connect_enabled = state.auto_connect_enabled;
  settings.slave_audio_send_enabled = state.audio_send_enabled;
  settings.slave_audio_format = state.audio_desired_format;
  settings.master_audio_overridden = state.audio_master_overridden;
  settings.master_audio_send_enabled = state.audio_master_desired_send_enabled;
  settings.master_audio_format = state.audio_master_desired_format;
  discovery::save_user_settings(settings);
}

std::string resolve_peer_name(const AppState& state, uint64_t device_id, const std::string& fallback_ip) {
  for (const auto& peer : state.listener->peers()) {
    if (peer.device_id == device_id) return peer.hostname;
  }
  return fallback_ip;
}

// Diagnostic key-monitor labels — covers the escape hotkey's own keys plus
// their left/right variants; anything else falls back to its raw VK code.
std::string vk_name(uint32_t vk) {
  switch (vk) {
    case 0x1B: return "Esc";
    case 0x10: case 0xA0: case 0xA1: return "Shift";
    case 0x11: case 0xA2: case 0xA3: return "Ctrl";
    case 0x12: case 0xA4: case 0xA5: return "Alt";
    case 0x5B: case 0x5C: return "Win";
    default: {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "VK 0x%02X", vk);
      return buf;
    }
  }
}

void draw_monitor_table(const char* table_id, const std::vector<display::MonitorInfo>& monitors) {
  if (monitors.empty()) {
    ImGui::TextUnformatted("(none reported)");
    return;
  }
  if (ImGui::BeginTable(table_id, 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(480, 0))) {
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("X");
    ImGui::TableSetupColumn("Y");
    ImGui::TableSetupColumn("Width");
    ImGui::TableSetupColumn("Height");
    ImGui::TableSetupColumn("Primary");
    ImGui::TableHeadersRow();
    for (const auto& m : monitors) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(m.name.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%d", m.x);
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%d", m.y);
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%d", m.width);
      ImGui::TableSetColumnIndex(4);
      ImGui::Text("%d", m.height);
      ImGui::TableSetColumnIndex(5);
      ImGui::TextUnformatted(m.primary ? "yes" : "");
    }
    ImGui::EndTable();
  }
}

// Diagnostics meant to help distinguish *why* a connection feels bad --
// network latency/instability (RTT), missing/late audio (playout misses),
// playback starvation (underruns), backlog in the jitter buffer, and
// reconnection history -- rather than just reporting that it is bad. Shown
// on both Master and Slave whenever a peer is connected; the audio-related
// rows only apply on Master, since playback (and therefore the jitter
// buffer/underruns/playout-miss counters) lives there in this
// single-direction-audio architecture.
void draw_connection_quality(AppState& state, const discovery::ConnectionInfo& info) {
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextUnformatted("Connection quality:");

  if (info.last_rtt_ms < 0) {
    ImGui::TextUnformatted("Round-trip time: measuring...");
  } else {
    ImGui::Text("Round-trip time: %lld ms", static_cast<long long>(info.last_rtt_ms));
  }

  if (state.role == discovery::Role::Master) {
    // "Since playback started" rather than "this connection": both
    // counters live on the current AudioPlayback/JitterBuffer instance,
    // which is reconstructed (and so restarts at 0) on a mid-connection
    // audio-quality change too, not just a fresh TCP connection -- see
    // audio_underruns_baseline's comment in app_state.h.
    const uint64_t misses = state.audio_playback ? state.audio_playback->playout_misses() : 0;
    const uint64_t played = state.audio_playback ? state.audio_playback->frames_played() : 0;
    const uint64_t attempts = misses + played;
    if (attempts > 0) {
      ImGui::Text("Audio playout misses (since playback started): %llu / %llu (%.1f%%)",
                  static_cast<unsigned long long>(misses), static_cast<unsigned long long>(attempts),
                  100.0 * static_cast<double>(misses) / static_cast<double>(attempts));
    } else {
      ImGui::Text("Audio playout misses (since playback started): %llu", static_cast<unsigned long long>(misses));
    }
    ImGui::TextUnformatted(
        "(Playback-side estimate: may reflect network loss, late arrival, or a receive stall -- not exact packet loss.)");

    const uint64_t underruns_total = telemetry::counters().audio_underruns.load(std::memory_order_relaxed);
    const uint64_t underruns = state.audio_playback && underruns_total >= state.audio_underruns_baseline
                                    ? underruns_total - state.audio_underruns_baseline
                                    : 0;
    ImGui::Text("Audio underruns (since playback started): %llu", static_cast<unsigned long long>(underruns));

    const size_t depth = state.audio_playback ? state.audio_playback->buffered_packets() : 0;
    const size_t capacity = state.audio_playback ? state.audio_playback->buffer_capacity() : 0;
    ImGui::Text("Jitter buffer: %zu / %zu packets", depth, capacity);
  } else {
    ImGui::TextUnformatted("Audio playout misses: not applicable (playback happens on the Master)");
    ImGui::TextUnformatted("Audio underruns: not applicable (playback happens on the Master)");
    ImGui::TextUnformatted("Jitter buffer: not applicable (playback happens on the Master)");
  }

  ImGui::Text("Reconnections this session: %u", state.reconnect_count);
}

void draw_connection_tab(AppState& state) {
  const discovery::Role wanted = state.role == discovery::Role::Master ? discovery::Role::Slave : discovery::Role::Master;
  const bool is_slave = state.role == discovery::Role::Slave;

  bool connection_settings_changed = ImGui::InputInt("Connection timeout (s)", &state.connection_timeout_s);
  const int connection_timeout_before_clamp = state.connection_timeout_s;
  state.connection_timeout_s = std::max(1, state.connection_timeout_s);
  connection_settings_changed = connection_settings_changed || state.connection_timeout_s != connection_timeout_before_clamp;
  if (connection_settings_changed) save_user_settings(state);
  ImGui::TextUnformatted(
      is_slave ? "(Give up auto-reconnecting after this many seconds of continuous disconnection.)"
               : "(Force-disconnect a silent Slave after this many seconds with no traffic.)");
  if (state.tcp_server) state.tcp_server->set_heartbeat_timeout(std::chrono::seconds(state.connection_timeout_s));

  if (is_slave) {
    if (ImGui::Checkbox("Auto-connect to trusted Masters", &state.auto_connect_enabled)) save_user_settings(state);
    ImGui::TextUnformatted("(Automatically connects to any discovered Master already in the trusted list below.)");
  }
  ImGui::Spacing();

  ImGui::Text("Discovered %s(s):", role_label(wanted));

  const int column_count = is_slave ? 5 : 4;
  if (ImGui::BeginTable("peers", column_count, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(560, 150))) {
    ImGui::TableSetupColumn("Hostname");
    ImGui::TableSetupColumn("Role");
    ImGui::TableSetupColumn("IP");
    ImGui::TableSetupColumn("Last seen");
    if (is_slave) ImGui::TableSetupColumn("Connect");
    ImGui::TableHeadersRow();

    const auto now = std::chrono::steady_clock::now();
    for (const auto& peer : state.listener->peers()) {
      if (peer.role != wanted) continue;
      const auto age_s = std::chrono::duration_cast<std::chrono::seconds>(now - peer.last_seen).count();
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(peer.hostname.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(role_label(peer.role));
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(peer.ip_address.c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%llds ago", static_cast<long long>(age_s));
      if (is_slave) {
        ImGui::TableSetColumnIndex(4);
        ImGui::PushID(static_cast<int>(peer.device_id));
        ImGui::BeginDisabled(peer.tcp_port == 0);
        if (ImGui::Button("Connect")) {
          state.tcp_client = std::make_unique<discovery::TcpClient>(
              state.device_id, peer.ip_address, peer.tcp_port, state.known_peers,
              std::chrono::seconds(state.connection_timeout_s));
          state.tcp_client->start();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }

  ImGui::Spacing();
  if (state.role == discovery::Role::Master && state.tcp_server) {
    ImGui::Text("Listening on port %u", state.tcp_server->port());
    const discovery::ConnectionInfo info = state.tcp_server->status();
    if (info.state == discovery::ConnectionState::Pairing) {
      ImGui::Text("Unknown device wants to pair. Fingerprint: %s", info.pairing_fingerprint.c_str());
      if (ImGui::Button("Accept")) state.tcp_server->approve_pairing();
      ImGui::SameLine();
      if (ImGui::Button("Reject")) state.tcp_server->reject_pairing();
    } else if (info.state == discovery::ConnectionState::Connected) {
      ImGui::Text("Connected: %s", resolve_peer_name(state, info.peer_device_id, info.peer_ip).c_str());
      if (ImGui::Button("Disconnect")) state.tcp_server->disconnect_current();
      ImGui::Text("Input control: %s", state.input_owned_by_master ? "Master" : "Slave");
      ImGui::TextUnformatted("(Ctrl+Alt+Shift+Esc forces control back to Master)");
      {
        std::string held;
        for (const HeldKey& k : state.input_held_keys) {
          if (!held.empty()) held += " + ";
          held += vk_name(k.vk);
        }
        ImGui::Text("Keys held: %s", held.empty() ? "(none)" : held.c_str());
      }
      // Diagnostic pair. Audio buffer sitting at its 200-packet ceiling
      // means Slave is outrunning this machine's playback device and frames
      // are being dropped to stay bounded; memory climbing steadily over a
      // session is the thing to catch if the machine starts getting slow
      // again.
      ImGui::Text("Audio buffer: %zu packets   |   Memory: %.0f MiB",
                  state.audio_playback ? state.audio_playback->buffered_packets() : 0u, resident_mib());
      draw_connection_quality(state, info);
      ImGui::Spacing();
      ImGui::TextUnformatted("Slave's displays:");
      draw_monitor_table("slave_monitors", info.peer_monitors);
    } else {
      ImGui::TextUnformatted("Waiting for a Slave to connect...");
    }
  } else if (is_slave && state.tcp_client) {
    const discovery::ConnectionInfo info = state.tcp_client->status();
    if (info.state == discovery::ConnectionState::Connecting) {
      ImGui::Text("Connecting to %s...", info.peer_ip.c_str());
    } else if (info.state == discovery::ConnectionState::Pairing) {
      ImGui::Text("Fingerprint: %s", info.pairing_fingerprint.c_str());
      ImGui::TextUnformatted("Waiting for the Master to approve...");
    } else if (info.state == discovery::ConnectionState::Connected) {
      ImGui::Text("Connected: %s", resolve_peer_name(state, info.peer_device_id, info.peer_ip).c_str());
      if (ImGui::Button("Disconnect")) {
        // "I've had enough of this guy" -- stop auto-connecting to this
        // one for the rest of the session, but Master's own trust of this
        // Slave is unaffected (that's Master's call, not Slave's).
        state.auto_connect_suppressed.insert(info.peer_device_id);
        state.tcp_client.reset();
      }
      draw_connection_quality(state, info);
    } else {
      ImGui::TextUnformatted("Connection failed");
    }
  }
}

void draw_audio_tab(AppState& state) {
  if (state.role == discovery::Role::Slave) {
    bool settings_changed = ImGui::Checkbox("Send audio", &state.audio_send_enabled);
    ImGui::Spacing();

    ImGui::TextUnformatted("Sample rate:");
    if (ImGui::RadioButton("48kHz", state.audio_desired_format.sample_rate == 48000)) {
      state.audio_desired_format.sample_rate = 48000;
      settings_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("24kHz", state.audio_desired_format.sample_rate == 24000)) {
      state.audio_desired_format.sample_rate = 24000;
      settings_changed = true;
    }

    ImGui::TextUnformatted("Bit depth:");
    if (ImGui::RadioButton("16-bit", state.audio_desired_format.bit_depth == 16)) {
      state.audio_desired_format.bit_depth = 16;
      settings_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("8-bit", state.audio_desired_format.bit_depth == 8)) {
      state.audio_desired_format.bit_depth = 8;
      settings_changed = true;
    }
    if (settings_changed) save_user_settings(state);

    ImGui::Spacing();
    if (state.audio_active) {
      ImGui::Text("Currently sending: %u Hz, %u-bit", state.audio_active_format.sample_rate,
                  state.audio_active_format.bit_depth);
    } else {
      ImGui::TextUnformatted("Not currently sending (not connected, or disabled above).");
    }
    ImGui::Spacing();
    ImGui::TextUnformatted("(These settings can also be changed from Master's side.)");
  } else {
    // Master can drive these settings too -- pump_audio() sends whatever is
    // set here to Slave as a request (kMsgAudioControl) once the user
    // actually touches one of these controls (audio_master_overridden).
    // Until then, they just mirror whatever Slave is actually doing (kept in
    // sync by pump_master()), so opening this tab shows the real current
    // state rather than Master's untouched default -- the fields below are
    // the same audio_master_desired_* the radio buttons above are bound to,
    // this section only decides when clicking one starts being a request.
    ImGui::TextUnformatted(state.audio_master_overridden ? "Requesting from Slave:" : "Slave's current settings:");
    bool settings_changed = false;
    if (ImGui::Checkbox("Send audio", &state.audio_master_desired_send_enabled)) {
      state.audio_master_overridden = true;
      settings_changed = true;
    }
    ImGui::Spacing();

    ImGui::TextUnformatted("Sample rate:");
    if (ImGui::RadioButton("48kHz", state.audio_master_desired_format.sample_rate == 48000)) {
      state.audio_master_desired_format.sample_rate = 48000;
      state.audio_master_overridden = true;
      settings_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("24kHz", state.audio_master_desired_format.sample_rate == 24000)) {
      state.audio_master_desired_format.sample_rate = 24000;
      state.audio_master_overridden = true;
      settings_changed = true;
    }

    ImGui::TextUnformatted("Bit depth:");
    if (ImGui::RadioButton("16-bit", state.audio_master_desired_format.bit_depth == 16)) {
      state.audio_master_desired_format.bit_depth = 16;
      state.audio_master_overridden = true;
      settings_changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("8-bit", state.audio_master_desired_format.bit_depth == 8)) {
      state.audio_master_desired_format.bit_depth = 8;
      state.audio_master_overridden = true;
      settings_changed = true;
    }
    if (settings_changed) save_user_settings(state);

    ImGui::Spacing();
    if (state.tcp_server && state.tcp_server->status().state == discovery::ConnectionState::Connected) {
      const discovery::ConnectionInfo info = state.tcp_server->status();
      if (info.peer_audio_send_enabled) {
        ImGui::Text("Slave is sending: %u Hz, %u-bit", info.peer_audio_sample_rate, info.peer_audio_bit_depth);
      } else {
        ImGui::TextUnformatted("Slave is not sending audio.");
      }
    } else {
      ImGui::TextUnformatted("No audio (Slave not connected).");
    }
  }
}

}  // namespace

void resume_last_role_if_any(AppState& state) {
  if (const auto role = discovery::get_last_role()) start_discovery(state, *role);
}

void draw_role_select(AppState& state) {
  begin_fullscreen_window();
  ImGui::Text("This machine: %s", state.hostname.c_str());
  ImGui::Text("Choose how this device should act on the LAN:");
  ImGui::Spacing();
  if (ImGui::Button("I am the Master", ImVec2(220, 40))) start_discovery(state, discovery::Role::Master);
  if (ImGui::Button("I am a Slave", ImVec2(220, 40))) start_discovery(state, discovery::Role::Slave);
  ImGui::Spacing();
  if (ImGui::Button("Manage known devices")) {
    state.previous_screen = Screen::RoleSelect;
    state.screen = Screen::ManageDevices;
  }
  ImGui::End();
}

void draw_discovery(AppState& state) {
  begin_fullscreen_window();
  ImGui::Text("Role: %s  |  Host: %s", role_label(state.role), state.hostname.c_str());
  ImGui::Separator();

  if (ImGui::BeginTabBar("discovery_tabs")) {
    if (ImGui::BeginTabItem("Connection")) {
      draw_connection_tab(state);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Audio")) {
      draw_audio_tab(state);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextUnformatted("This machine's displays:");
  draw_monitor_table("local_monitors", state.local_monitors);

  ImGui::Spacing();
  if (ImGui::Button("Manage known devices")) {
    state.previous_screen = Screen::Discovery;
    state.screen = Screen::ManageDevices;
  }
  if (state.role == discovery::Role::Master) {
    ImGui::SameLine();
    if (ImGui::Button("Arrange screens")) {
      state.previous_screen = Screen::Discovery;
      state.screen = Screen::Arrangement;
    }
  }

  ImGui::Spacing();
  if (ImGui::Button("Back")) {
    state.announcer.reset();
    state.listener.reset();
    state.tcp_server.reset();
    state.tcp_client.reset();
    state.screen = Screen::RoleSelect;
  }
  ImGui::End();
}

void draw_manage_devices(AppState& state) {
  begin_fullscreen_window();
  ImGui::TextUnformatted("Known devices:");
  ImGui::Spacing();

  if (ImGui::BeginTable("known_peers", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(420, 150))) {
    ImGui::TableSetupColumn("Device ID");
    ImGui::TableSetupColumn("Key");
    ImGui::TableSetupColumn("Forget");
    ImGui::TableHeadersRow();

    for (const auto& entry : state.known_peers.list()) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%016llx", static_cast<unsigned long long>(entry.device_id));
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%02x%02x%02x%02x...", entry.pubkey[0], entry.pubkey[1], entry.pubkey[2], entry.pubkey[3]);
      ImGui::TableSetColumnIndex(2);
      ImGui::PushID(static_cast<int>(entry.device_id));
      if (ImGui::Button("Forget")) {
        state.known_peers.forget(entry.device_id);
        if (state.tcp_server && state.tcp_server->status().state == discovery::ConnectionState::Connected &&
            state.tcp_server->status().peer_device_id == entry.device_id) {
          state.tcp_server->disconnect_current();
        }
        if (state.tcp_client && state.tcp_client->status().state == discovery::ConnectionState::Connected &&
            state.tcp_client->status().peer_device_id == entry.device_id) {
          state.tcp_client.reset();
        }
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::Spacing();
  if (ImGui::Button("Back")) state.screen = state.previous_screen;
  ImGui::End();
}

void draw_arrangement(AppState& state) {
  begin_fullscreen_window();
  ImGui::TextUnformatted("Screen arrangement:");
  ImGui::TextUnformatted("Drag the Slave block to a side of the Master block, then release to snap.");
  ImGui::Spacing();

  if (state.local_monitors.empty()) {
    ImGui::TextUnformatted("This machine reported no displays.");
    ImGui::Spacing();
    if (ImGui::Button("Back")) state.screen = state.previous_screen;
    ImGui::End();
    return;
  }

  constexpr float kScale = 0.08f;  // canvas px per real px

  const topology::ClusterBounds master_bounds = topology::compute_bounds(state.local_monitors);
  const float master_w = static_cast<float>(master_bounds.max_x - master_bounds.min_x) * kScale;
  const float master_h = static_cast<float>(master_bounds.max_y - master_bounds.min_y) * kScale;

  discovery::ConnectionInfo info;
  bool has_peer = false;
  if (state.tcp_server) {
    info = state.tcp_server->status();
    has_peer = info.state == discovery::ConnectionState::Connected && !info.peer_monitors.empty();
  }

  topology::ClusterBounds peer_bounds{};
  float peer_w = 0.0f, peer_h = 0.0f;
  topology::Direction direction = topology::Direction::Right;
  int32_t offset = 0;
  if (has_peer) {
    peer_bounds = topology::compute_bounds(info.peer_monitors);
    peer_w = static_cast<float>(peer_bounds.max_x - peer_bounds.min_x) * kScale;
    peer_h = static_cast<float>(peer_bounds.max_y - peer_bounds.min_y) * kScale;
    if (const auto saved = state.screen_arrangement.get(info.peer_device_id)) {
      direction = saved->direction;
      offset = saved->offset;
    }
  }

  // Static drag state: only one peer block is ever draggable at a time
  // (single active connection), keyed by device_id so a stale drag from a
  // previously-connected peer can't leak into a new one.
  static bool dragging = false;
  static uint64_t dragging_device_id = 0;
  static ImVec2 drag_origin_real{0.0f, 0.0f};
  // A disconnect mid-drag (mouse still held) means IsItemDeactivated() will
  // never fire for this widget again; without this, a later reconnect to
  // the same device_id would render at the stale, never-saved drag position
  // instead of the correctly persisted one.
  if (!has_peer) dragging = false;

  ImVec2 peer_origin_real{0.0f, 0.0f};  // top-left of the peer block, in Master's own coordinate space
  if (has_peer) {
    const float peer_width = static_cast<float>(peer_bounds.max_x - peer_bounds.min_x);
    const float peer_height = static_cast<float>(peer_bounds.max_y - peer_bounds.min_y);
    switch (direction) {
      case topology::Direction::Right:
        peer_origin_real = ImVec2(static_cast<float>(master_bounds.max_x), static_cast<float>(offset));
        break;
      case topology::Direction::Left:
        peer_origin_real = ImVec2(static_cast<float>(master_bounds.min_x) - peer_width, static_cast<float>(offset));
        break;
      case topology::Direction::Down:
        peer_origin_real = ImVec2(static_cast<float>(offset), static_cast<float>(master_bounds.max_y));
        break;
      case topology::Direction::Up:
        peer_origin_real = ImVec2(static_cast<float>(offset), static_cast<float>(master_bounds.min_y) - peer_height);
        break;
    }
    if (dragging && dragging_device_id == info.peer_device_id) peer_origin_real = drag_origin_real;
  }

  // Combined bounds across both blocks, so the canvas frames whichever side the peer is on.
  float combined_min_x = static_cast<float>(master_bounds.min_x);
  float combined_max_x = static_cast<float>(master_bounds.max_x);
  float combined_min_y = static_cast<float>(master_bounds.min_y);
  float combined_max_y = static_cast<float>(master_bounds.max_y);
  if (has_peer) {
    combined_min_x = std::min(combined_min_x, peer_origin_real.x);
    combined_max_x = std::max(combined_max_x, peer_origin_real.x + peer_w / kScale);
    combined_min_y = std::min(combined_min_y, peer_origin_real.y);
    combined_max_y = std::max(combined_max_y, peer_origin_real.y + peer_h / kScale);
  }

  const ImVec2 canvas_size((combined_max_x - combined_min_x) * kScale + 40.0f,
                            (combined_max_y - combined_min_y) * kScale + 40.0f);

  // A real child region (not just a Dummy sized to fit): the canvas can
  // legitimately be bigger than the available window space (real monitor
  // pixel dimensions, only scaled down by kScale), so it needs its own
  // scrollbars rather than growing the whole screen to fit it.
  ImGui::BeginChild("canvas_region", ImVec2(0.0f, 320.0f), true, ImGuiWindowFlags_HorizontalScrollbar);

  // Dummy (not InvisibleButton): this only needs to reserve layout space so
  // the child sizes its scroll range correctly. An InvisibleButton here
  // would be a full-canvas interactive widget submitted before peer_block,
  // so it would claim mouse-down capture on every click before peer_block
  // (at the same screen position) ever gets a chance — silently eating the
  // drag.
  ImGui::Dummy(canvas_size);
  const ImVec2 canvas_origin = ImGui::GetItemRectMin();

  auto to_canvas = [&](float real_x, float real_y) {
    return ImVec2(canvas_origin.x + (real_x - combined_min_x) * kScale,
                  canvas_origin.y + (real_y - combined_min_y) * kScale);
  };

  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  const ImVec2 master_p0 = to_canvas(static_cast<float>(master_bounds.min_x), static_cast<float>(master_bounds.min_y));
  const ImVec2 master_p1(master_p0.x + master_w, master_p0.y + master_h);
  draw_list->AddRectFilled(master_p0, master_p1, IM_COL32(70, 110, 180, 255));
  draw_list->AddText(ImVec2(master_p0.x + 4.0f, master_p0.y + 4.0f), IM_COL32(255, 255, 255, 255), "Master");

  if (has_peer) {
    const ImVec2 peer_p0 = to_canvas(peer_origin_real.x, peer_origin_real.y);
    const ImVec2 peer_p1(peer_p0.x + peer_w, peer_p0.y + peer_h);
    draw_list->AddRectFilled(peer_p0, peer_p1, IM_COL32(180, 110, 70, 255));
    draw_list->AddText(ImVec2(peer_p0.x + 4.0f, peer_p0.y + 4.0f), IM_COL32(255, 255, 255, 255), "Slave");

    ImGui::SetCursorScreenPos(peer_p0);
    ImGui::InvisibleButton("peer_block", ImVec2(peer_w, peer_h));

    if (ImGui::IsItemActivated()) {
      dragging = true;
      dragging_device_id = info.peer_device_id;
      drag_origin_real = peer_origin_real;
    }
    if (dragging && dragging_device_id == info.peer_device_id) {
      if (ImGui::IsItemActive()) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        drag_origin_real.x += delta.x / kScale;
        drag_origin_real.y += delta.y / kScale;
      }
      if (ImGui::IsItemDeactivated()) {
        const float peer_width = static_cast<float>(peer_bounds.max_x - peer_bounds.min_x);
        const float peer_height = static_cast<float>(peer_bounds.max_y - peer_bounds.min_y);
        const topology::PlacementDecision decision =
            topology::decide_placement(master_bounds, peer_width, peer_height, drag_origin_real.x, drag_origin_real.y);
        state.screen_arrangement.set(info.peer_device_id, decision.direction, decision.offset);
        dragging = false;
      }
    }
  }

  ImGui::EndChild();

  ImGui::Spacing();
  if (!has_peer) ImGui::TextUnformatted("Connect a Slave to arrange its screens.");

  ImGui::Spacing();
  if (ImGui::Button("Back")) state.screen = state.previous_screen;
  ImGui::End();
}

}  // namespace nockvm::app
