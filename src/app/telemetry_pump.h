#pragma once
#include "app_state.h"

namespace nockvm::app {

// Drives the process-wide telemetry system (nockvm::telemetry) once per
// frame: logs connection state transitions, then feeds this frame's
// timing/visibility into telemetry::tick() for the periodic summary.
// frame_time_ms is measured by main.cpp around the whole loop body;
// window_visible comes from querying GLFW directly (glfwGetWindowAttrib)
// rather than tracking it by hand across main.cpp/tray.cpp/tray_linux.cpp.
void pump_telemetry(AppState& state, double frame_time_ms, bool window_visible);

}  // namespace nockvm::app
