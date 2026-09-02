#pragma once
#include <memory>
#include <string>
#include <vector>
#include "nockvm/discovery/announcer.h"
#include "nockvm/discovery/known_peers.h"
#include "nockvm/discovery/listener.h"
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
};

}  // namespace nockvm::app
