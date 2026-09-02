#pragma once
#include <cstdint>
#include <vector>
#include "nockvm/display/monitor_info.h"
#include "nockvm/topology/arrangement.h"

namespace nockvm::topology {

struct ClusterBounds {
  int32_t min_x, max_x, min_y, max_y;
};

// The bounding box of a machine's whole monitor set, in its own coordinate
// space. Used both for crossing computation and for rendering a machine's
// cluster as a single block. Callers must not pass an empty list.
ClusterBounds compute_bounds(const std::vector<display::MonitorInfo>& monitors);

struct CrossingResult {
  bool has_target = false;  // false only when peer_monitors is empty (headless peer)
  int32_t x = 0, y = 0;     // landing position in the peer's own coordinate space
};

// master_perp_pos: the cursor's coordinate along the axis perpendicular to
// the crossing edge (Y for Left/Right, X for Up/Down), in Master's own
// coordinate space — no transform needed since Master's local monitor list
// already uses that space directly.
//
// Always crosses (unless the peer has no monitors at all); the landing
// point is clamped into whatever the peer's monitor union currently covers,
// mirroring native OS multi-monitor cursor clamping rather than refusing to
// cross when the peer's cluster doesn't fully line up with Master's edge.
CrossingResult compute_crossing(const std::vector<display::MonitorInfo>& peer_monitors, Direction direction,
                                 int32_t offset, int32_t master_perp_pos);

struct PlacementDecision {
  Direction direction;
  int32_t offset;
};

// Given a dragged block's top-left origin (in Master's own coordinate
// space, matching how peer blocks are positioned for compute_crossing) and
// its size, decides which of the four cardinal directions is dominant
// (whichever axis has the larger displacement between the two blocks'
// centers) and the resulting perpendicular-axis offset to persist. Used by
// the arrangement UI's drag-and-drop; factored out as a pure function so
// the decision logic is unit-testable independent of ImGui/mouse input.
PlacementDecision decide_placement(ClusterBounds master_bounds, float peer_width, float peer_height,
                                    float drag_origin_x, float drag_origin_y);

}  // namespace nockvm::topology
