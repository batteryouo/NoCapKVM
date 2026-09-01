#include "ui.h"
#include <chrono>
#include <string>
#include <imgui.h>
#include "nockvm/discovery/connection_types.h"
#include "nockvm/discovery/types.h"

namespace nockvm::app {
namespace {

void start_discovery(AppState& state, discovery::Role role) {
  state.role = role;

  uint16_t tcp_port = 0;
  if (role == discovery::Role::Master) {
    state.tcp_server = std::make_unique<discovery::TcpServer>(state.device_id);
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

}  // namespace

void draw_role_select(AppState& state) {
  ImGui::Begin("NoCapKVM", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
  ImGui::Text("This machine: %s", state.hostname.c_str());
  ImGui::Text("Choose how this device should act on the LAN:");
  ImGui::Spacing();
  if (ImGui::Button("I am the Master", ImVec2(220, 40))) start_discovery(state, discovery::Role::Master);
  if (ImGui::Button("I am a Slave", ImVec2(220, 40))) start_discovery(state, discovery::Role::Slave);
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
          state.tcp_client = std::make_unique<discovery::TcpClient>(state.device_id, peer.ip_address, peer.tcp_port);
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
    if (info.state == discovery::ConnectionState::Connected) {
      ImGui::Text("Connected: %s", resolve_peer_name(state, info.peer_device_id, info.peer_ip).c_str());
    } else {
      ImGui::TextUnformatted("Waiting for a Slave to connect...");
    }
  } else if (is_slave && state.tcp_client) {
    const discovery::ConnectionInfo info = state.tcp_client->status();
    if (info.state == discovery::ConnectionState::Connecting) {
      ImGui::Text("Connecting to %s...", info.peer_ip.c_str());
    } else if (info.state == discovery::ConnectionState::Connected) {
      ImGui::Text("Connected: %s", resolve_peer_name(state, info.peer_device_id, info.peer_ip).c_str());
    } else {
      ImGui::TextUnformatted("Connection failed");
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

}  // namespace nockvm::app
