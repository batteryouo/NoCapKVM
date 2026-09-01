#pragma once
#include <memory>
#include <string>
#include "nockvm/discovery/announcer.h"
#include "nockvm/discovery/known_peers.h"
#include "nockvm/discovery/listener.h"
#include "nockvm/discovery/tcp_client.h"
#include "nockvm/discovery/tcp_server.h"
#include "nockvm/discovery/types.h"

namespace nockvm::app {

enum class Screen { RoleSelect, Discovery, ManageDevices };

struct AppState {
  Screen screen = Screen::RoleSelect;
  Screen previous_screen = Screen::RoleSelect;  // where "Back" on ManageDevices returns to
  discovery::Role role = discovery::Role::Master;
  uint64_t device_id = 0;
  std::string hostname;
  discovery::KnownPeers known_peers;  // loaded once at startup, shared by reference
  std::unique_ptr<discovery::Announcer> announcer;
  std::unique_ptr<discovery::Listener> listener;
  std::unique_ptr<discovery::TcpServer> tcp_server;  // Master only
  std::unique_ptr<discovery::TcpClient> tcp_client;  // Slave only, set on Connect click
};

}  // namespace nockvm::app
