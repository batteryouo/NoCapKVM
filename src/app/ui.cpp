#include "ui.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <imgui.h>
#include "nockvm/discovery/connection_types.h"
#include "nockvm/discovery/types.h"
#include "nockvm/topology/crossing.h"

namespace nockvm::app {
namespace {

void start_discovery(AppState& state, discovery::Role role) {
  state.role = role;

  uint16_t tcp_port = 0;
  if (role == discovery::Role::Master) {
    state.tcp_server = std::make_unique<discovery::TcpServer>(state.device_id, state.known_peers);
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

std::string resolve_peer_name(const AppState& state, uint64_t device_id, const std::string& fallback_ip) {
  for (const auto& peer : state.listener->peers()) {
    if (peer.device_id == device_id) return peer.hostname;
  }
  return fallback_ip;
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

}  // namespace

void draw_role_select(AppState& state) {
  ImGui::Begin("NoCapKVM", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
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
  ImGui::Begin("NoCapKVM", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
  ImGui::Text("Role: %s  |  Host: %s", role_label(state.role), state.hostname.c_str());
  ImGui::Separator();

  const discovery::Role wanted = state.role == discovery::Role::Master ? discovery::Role::Slave : discovery::Role::Master;
  const bool is_slave = state.role == discovery::Role::Slave;
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
          state.tcp_client = std::make_unique<discovery::TcpClient>(state.device_id, peer.ip_address, peer.tcp_port,
                                                                      state.known_peers);
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
      if (ImGui::Button("Disconnect")) state.tcp_client.reset();
    } else {
      ImGui::TextUnformatted("Connection failed");
    }
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
  ImGui::Begin("NoCapKVM", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
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
  ImGui::Begin("NoCapKVM", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
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
  // Dummy (not InvisibleButton): this only needs to reserve layout space so
  // the auto-resize window sizes correctly. An InvisibleButton here would be
  // a full-canvas interactive widget submitted before peer_block, so it
  // would claim mouse-down capture on every click before peer_block (at the
  // same screen position) ever gets a chance — silently eating the drag.
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

  // Restore normal layout flow below the whole canvas (drawing/dragging above manipulated the cursor directly).
  ImGui::SetCursorScreenPos(ImVec2(canvas_origin.x, canvas_origin.y + canvas_size.y));

  ImGui::Spacing();
  if (!has_peer) ImGui::TextUnformatted("Connect a Slave to arrange its screens.");

  ImGui::Spacing();
  if (ImGui::Button("Back")) state.screen = state.previous_screen;
  ImGui::End();
}

}  // namespace nockvm::app
