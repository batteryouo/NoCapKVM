#pragma once
#include "app_state.h"

namespace nockvm::app {

// Drives audio routing (brief §3.3, Slave -> Master) off the Connected
// transition, mirroring pump_input()'s own install/uninstall pattern. Call
// once per frame; no-ops on whichever side doesn't apply to the current
// role.
void pump_audio(AppState& state);

}  // namespace nockvm::app
