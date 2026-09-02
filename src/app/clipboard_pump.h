#pragma once
#include "app_state.h"

namespace nockvm::app {

// Syncs the OS clipboard (text and images, images re-encoded as JPEG) with
// whichever peer is currently Connected -- bidirectional, unlike audio's
// Slave-only capture. Call once per frame; no-ops when nothing is
// Connected.
void pump_clipboard(AppState& state);

}  // namespace nockvm::app
