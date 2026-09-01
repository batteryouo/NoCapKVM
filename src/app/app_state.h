#pragma once
#include <memory>
#include <string>
#include "nockvm/discovery/announcer.h"
#include "nockvm/discovery/listener.h"
#include "nockvm/discovery/tcp_client.h"
#include "nockvm/discovery/tcp_server.h"
#include "nockvm/discovery/types.h"

namespace nockvm::app {

enum class Screen { RoleSelect, Discovery };

struct AppState {
  Screen screen = Screen::RoleSelect;
  discovery::Role role = discovery::Role::Master;
  uint64_t device_id = 0;
  std::string hostname;
  std::unique_ptr<discovery::Announcer> announcer;
  std::unique_ptr<discovery::Listener> listener;
  std::unique_ptr<discovery::TcpServer> tcp_server;  // Master only
  std::unique_ptr<discovery::TcpClient> tcp_client;  // Slave only, set on Connect click
};

}  // namespace nockvm::app
