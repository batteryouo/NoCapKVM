#include "nockvm/topology/crossing.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace nockvm::topology {
namespace {

int32_t clamp(int32_t value, int32_t lo, int32_t hi) { return std::min(std::max(value, lo), hi); }

}  // namespace

ClusterBounds compute_bounds(const std::vector<display::MonitorInfo>& monitors) {
  ClusterBounds b{std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min(),
                  std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min()};
  for (const auto& m : monitors) {
    b.min_x = std::min(b.min_x, m.x);
    b.max_x = std::max(b.max_x, m.x + m.width);
    b.min_y = std::min(b.min_y, m.y);
    b.max_y = std::max(b.max_y, m.y + m.height);
  }
  return b;
}

CrossingResult compute_crossing(const std::vector<display::MonitorInfo>& peer_monitors, Direction direction,
                                 int32_t offset, int32_t master_perp_pos) {
  CrossingResult result;
  if (peer_monitors.empty()) return result;

  const ClusterBounds b = compute_bounds(peer_monitors);
  result.has_target = true;

  if (direction == Direction::Left || direction == Direction::Right) {
    const int32_t extent = b.max_y - b.min_y;
    const int32_t shared = clamp(master_perp_pos, offset, offset + extent);
    result.y = shared - offset + b.min_y;
    result.x = direction == Direction::Right ? b.min_x : b.max_x;
  } else {
    const int32_t extent = b.max_x - b.min_x;
    const int32_t shared = clamp(master_perp_pos, offset, offset + extent);
    result.x = shared - offset + b.min_x;
    result.y = direction == Direction::Down ? b.min_y : b.max_y;
  }

  return result;
}

PlacementDecision decide_placement(ClusterBounds master_bounds, float peer_width, float peer_height,
                                    float drag_origin_x, float drag_origin_y) {
  const float peer_center_x = drag_origin_x + peer_width / 2.0f;
  const float peer_center_y = drag_origin_y + peer_height / 2.0f;
  const float master_center_x = static_cast<float>(master_bounds.min_x + master_bounds.max_x) / 2.0f;
  const float master_center_y = static_cast<float>(master_bounds.min_y + master_bounds.max_y) / 2.0f;
  const float dx = peer_center_x - master_center_x;
  const float dy = peer_center_y - master_center_y;

  PlacementDecision decision{};
  if (std::abs(dx) >= std::abs(dy)) {
    decision.direction = dx >= 0.0f ? Direction::Right : Direction::Left;
    decision.offset = static_cast<int32_t>(drag_origin_y);
  } else {
    decision.direction = dy >= 0.0f ? Direction::Down : Direction::Up;
    decision.offset = static_cast<int32_t>(drag_origin_x);
  }
  return decision;
}

BoundaryCheck check_boundary(ClusterBounds bounds, int32_t x, int32_t y) {
  BoundaryCheck result;
  result.clamped_x = clamp(x, bounds.min_x, bounds.max_x);
  result.clamped_y = clamp(y, bounds.min_y, bounds.max_y);

  if (x < bounds.min_x) {
    result.crossed = true;
    result.direction = Direction::Left;
    result.perp_pos = result.clamped_y;
  } else if (x > bounds.max_x) {
    result.crossed = true;
    result.direction = Direction::Right;
    result.perp_pos = result.clamped_y;
  } else if (y < bounds.min_y) {
    result.crossed = true;
    result.direction = Direction::Up;
    result.perp_pos = result.clamped_x;
  } else if (y > bounds.max_y) {
    result.crossed = true;
    result.direction = Direction::Down;
    result.perp_pos = result.clamped_x;
  }

  return result;
}

namespace {

Direction opposite(Direction d) {
  switch (d) {
    case Direction::Left: return Direction::Right;
    case Direction::Right: return Direction::Left;
    case Direction::Up: return Direction::Down;
    default: return Direction::Up;
  }
}

}  // namespace

ArrangementEntry invert_entry(ArrangementEntry entry, ClusterBounds master_bounds, ClusterBounds peer_bounds) {
  ArrangementEntry inv;
  inv.device_id = entry.device_id;
  inv.direction = opposite(entry.direction);
  if (entry.direction == Direction::Left || entry.direction == Direction::Right) {
    inv.offset = peer_bounds.min_y + master_bounds.min_y - entry.offset;
  } else {
    inv.offset = peer_bounds.min_x + master_bounds.min_x - entry.offset;
  }
  return inv;
}

}  // namespace nockvm::topology
