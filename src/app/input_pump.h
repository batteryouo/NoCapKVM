#pragma once
#include "app_state.h"

namespace nockvm::app {

// Master-only per-frame input capture/handoff state machine (brief §3.2).
// No-op unless state.role == Master. Call once per frame, right after
// glfwPollEvents().
void pump_input(AppState& state);

}  // namespace nockvm::app
