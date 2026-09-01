#pragma once
#include <memory>
#include <string>
#include "nockvm/discovery/announcer.h"
#include "nockvm/discovery/listener.h"
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
};

}  // namespace nockvm::app
