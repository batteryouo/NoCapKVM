#pragma once
#include <cstdint>
#include <utility>
#include <vector>
#include "nockvm/discovery/tcp_server.h"
#include "nockvm/display/monitor_info.h"
#include "nockvm/input/hook.h"
#include "nockvm/topology/arrangement.h"

namespace nockvm::app {

// A currently held key, including the data needed to release it remotely.
struct HeldKey {
  uint32_t vk = 0;
  uint32_t scancode = 0;
  bool extended = false;
  bool operator==(const HeldKey&) const = default;
};

// What the UI shows about Master-side input ownership.
struct InputStatus {
  bool owned_by_master = true;
  std::vector<HeldKey> held_keys;
  bool operator==(const InputStatus&) const = default;
};

// Master-side input capture and handoff state for one TcpServer. Every
// member is owned by the thread that runs pump_input() on it, which must
// also be the thread pumping Windows messages for `hook`.
struct InputSession {
  InputSession(discovery::TcpServer& server, const topology::ScreenArrangement& arrangement,
               std::vector<display::MonitorInfo> local_monitors)
      : server(server), arrangement(arrangement), local_monitors(std::move(local_monitors)) {}

  discovery::TcpServer& server;
  const topology::ScreenArrangement& arrangement;
  const std::vector<display::MonitorInfo> local_monitors;
  input::InputHook hook;
  bool hook_active = false;
  bool owned_by_master = true;
  int32_t logical_x = 0, logical_y = 0;
  // Suppresses boundary detection for the step after a handoff.
  bool just_handed_off = false;
  // Keys held on the current input owner.
  std::vector<HeldKey> held_keys;
};

// One capture/handoff step (brief §3.2): installs or removes the hooks as the
// connection comes and goes, drains captured input, and forwards it or
// hands control across a screen edge.
void pump_input(InputSession& s);

// Returns control to Master and removes the hooks, if installed.
void deactivate_input(InputSession& s);

InputStatus input_status(const InputSession& s);

}  // namespace nockvm::app
