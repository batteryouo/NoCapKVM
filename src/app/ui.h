#pragma once
#include "app_state.h"

namespace nockvm::app {

// If a role was saved from a previous run (see identity.h's
// get_last_role()/save_last_role()), jumps straight into Discovery as that
// role instead of showing RoleSelect -- session memory across restarts.
// No-op (leaves state.screen at its default, RoleSelect) on a genuine
// first run.
void resume_last_role_if_any(AppState& state);

void draw_role_select(AppState& state);
void draw_discovery(AppState& state);
void draw_manage_devices(AppState& state);
void draw_arrangement(AppState& state);

}  // namespace nockvm::app
