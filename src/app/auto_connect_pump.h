#pragma once
#include "app_state.h"

namespace nockvm::app {

// Slave-only: if state.auto_connect_enabled and there's no connection
// attempt already in progress, automatically starts one toward the first
// discovered Master that's already in known_peers (the existing TOFU trust
// store doubles as the "trusted list" here -- no separate one needed). No-op
// unless state.role == Slave. Call once per frame, alongside pump_input()/
// pump_audio().
void pump_auto_connect(AppState& state);

}  // namespace nockvm::app
