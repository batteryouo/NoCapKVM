#include "ui.h"
#include <chrono>
#include <imgui.h>
#include "nockvm/discovery/types.h"

namespace nockvm::app {
namespace {

void start_discovery(AppState& state, discovery::Role role) {
  state.role = role;
  state.announcer = std::make_unique<discovery::Announcer>(state.device_id, role, 0, state.hostname);
  state.listener = std::make_unique<discovery::Listener>(state.device_id);
  state.announcer->start();
  state.listener->start();
  state.screen = Screen::Discovery;
}

const char* role_label(discovery::Role role) { return role == discovery::Role::Master ? "Master" : "Slave"; }

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
  ImGui::Text("Discovered %s(s):", role_label(wanted));

  if (ImGui::BeginTable("peers", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(500, 150))) {
    ImGui::TableSetupColumn("Hostname");
    ImGui::TableSetupColumn("Role");
    ImGui::TableSetupColumn("IP");
    ImGui::TableSetupColumn("Last seen");
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
    }
    ImGui::EndTable();
  }

  ImGui::Spacing();
  if (ImGui::Button("Back")) {
    state.announcer.reset();
    state.listener.reset();
    state.screen = Screen::RoleSelect;
  }
  ImGui::End();
}

}  // namespace nockvm::app
