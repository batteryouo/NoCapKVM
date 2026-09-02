#include <cassert>
#include <cstdint>
#include <filesystem>
#include "nockvm/topology/arrangement.h"
#include "nockvm/topology/crossing.h"

using namespace nockvm::topology;
using nockvm::display::MonitorInfo;

namespace {

void set_env(const char* name, const std::filesystem::path& value) {
#ifdef _WIN32
  _putenv_s(name, value.string().c_str());
#else
  setenv(name, value.string().c_str(), 1);
#endif
}

MonitorInfo make_monitor(int32_t x, int32_t y, int32_t width, int32_t height) {
  MonitorInfo m;
  m.x = x;
  m.y = y;
  m.width = width;
  m.height = height;
  return m;
}

}  // namespace

int main() {
  // ScreenArrangement: set/get/forget/list round-trip, persisted across a
  // fresh instance.
  {
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "nockvm_test_arrangement";
    std::filesystem::remove_all(scratch);
    set_env("NOCKVM_HOME", scratch);

    {
      ScreenArrangement arrangement;
      assert(!arrangement.get(1).has_value());
      arrangement.set(1, Direction::Right, 0);
      arrangement.set(2, Direction::Up, -240);
      assert(arrangement.list().size() == 2);

      const auto e1 = arrangement.get(1);
      assert(e1.has_value());
      assert(e1->direction == Direction::Right);
      assert(e1->offset == 0);

      arrangement.forget(1);
      assert(!arrangement.get(1).has_value());
      assert(arrangement.get(2).has_value());
      assert(arrangement.list().size() == 1);
    }
    {
      // Forgetting and the surviving entry must both persist.
      ScreenArrangement arrangement2;
      assert(!arrangement2.get(1).has_value());
      const auto e2 = arrangement2.get(2);
      assert(e2.has_value());
      assert(e2->direction == Direction::Up);
      assert(e2->offset == -240);
    }

    std::filesystem::remove_all(scratch);
  }

  // compute_crossing: exact-fit case, Right direction.
  {
    // Peer cluster exactly matches Master's Y extent (0..1080), offset 0.
    const std::vector<MonitorInfo> peer = {make_monitor(0, 0, 1920, 1080)};
    const CrossingResult r = compute_crossing(peer, Direction::Right, 0, 540);
    assert(r.has_target);
    assert(r.x == 0);   // peer's leftmost edge
    assert(r.y == 540);  // linear, no clamping needed
  }

  // compute_crossing: partial coverage / clamp case (the (a) example from
  // discussion: Master 0..1440, peer covers only 100..740 of that range).
  {
    const std::vector<MonitorInfo> peer = {make_monitor(0, 0, 1920, 640)};  // height 640, i.e. 100..740 once offset
    const int32_t offset = 100;
    // Master's crossing position is within [0,100): below the peer's covered range -> clamps to the top.
    {
      const CrossingResult r = compute_crossing(peer, Direction::Right, offset, 50);
      assert(r.has_target);
      assert(r.y == 0);  // clamped to peer's min_y
    }
    // Master's crossing position is inside the covered range -> linear mapping.
    {
      const CrossingResult r = compute_crossing(peer, Direction::Right, offset, 400);
      assert(r.has_target);
      assert(r.y == 300);  // 400 - 100 + 0
    }
    // Master's crossing position is past the peer's covered range (>740) -> clamps to the bottom.
    {
      const CrossingResult r = compute_crossing(peer, Direction::Right, offset, 1000);
      assert(r.has_target);
      assert(r.y == 640);  // clamped to peer's max_y
    }
  }

  // compute_crossing: all four directions pick the correct fixed edge.
  {
    const std::vector<MonitorInfo> peer = {make_monitor(0, 0, 1000, 500)};

    {
      const CrossingResult r = compute_crossing(peer, Direction::Right, 0, 250);
      assert(r.x == 0);  // peer's min_x
    }
    {
      const CrossingResult r = compute_crossing(peer, Direction::Left, 0, 250);
      assert(r.x == 1000);  // peer's max_x
    }
    {
      const CrossingResult r = compute_crossing(peer, Direction::Down, 0, 500);
      assert(r.y == 0);  // peer's min_y
    }
    {
      const CrossingResult r = compute_crossing(peer, Direction::Up, 0, 500);
      assert(r.y == 500);  // peer's max_y
    }
  }

  // compute_crossing: headless peer (no monitors) -> no target.
  {
    const CrossingResult r = compute_crossing({}, Direction::Right, 0, 0);
    assert(!r.has_target);
  }

  // decide_placement: dropped clearly to the right of Master.
  {
    // Master spans 0..1920 x 0..1080 (center 960,540). Peer (200x100) dropped
    // far to the right, roughly Y-aligned with Master's top.
    const ClusterBounds master{0, 1920, 0, 1080};
    const PlacementDecision d = decide_placement(master, 200.0f, 100.0f, 2500.0f, 50.0f);
    assert(d.direction == Direction::Right);
    assert(d.offset == 50);  // offset is the drag origin's perpendicular (Y) coordinate, unchanged
  }

  // decide_placement: dropped clearly to the left.
  {
    const ClusterBounds master{0, 1920, 0, 1080};
    const PlacementDecision d = decide_placement(master, 200.0f, 100.0f, -2500.0f, 300.0f);
    assert(d.direction == Direction::Left);
    assert(d.offset == 300);
  }

  // decide_placement: dropped clearly above.
  {
    const ClusterBounds master{0, 1920, 0, 1080};
    const PlacementDecision d = decide_placement(master, 200.0f, 100.0f, 400.0f, -2000.0f);
    assert(d.direction == Direction::Up);
    assert(d.offset == 400);  // offset is the drag origin's perpendicular (X) coordinate
  }

  // decide_placement: dropped clearly below.
  {
    const ClusterBounds master{0, 1920, 0, 1080};
    const PlacementDecision d = decide_placement(master, 200.0f, 100.0f, 700.0f, 2000.0f);
    assert(d.direction == Direction::Down);
    assert(d.offset == 700);
  }

  // check_boundary: inside bounds -> no crossing, clamp is a no-op.
  {
    const ClusterBounds bounds{0, 1920, 0, 1080};
    const BoundaryCheck bc = check_boundary(bounds, 960, 540);
    assert(!bc.crossed);
    assert(bc.clamped_x == 960);
    assert(bc.clamped_y == 540);
  }

  // check_boundary: exceeds each of the four edges in turn.
  {
    const ClusterBounds bounds{0, 1920, 0, 1080};
    {
      const BoundaryCheck bc = check_boundary(bounds, -50, 300);
      assert(bc.crossed);
      assert(bc.direction == Direction::Left);
      assert(bc.clamped_x == 0);
      assert(bc.perp_pos == 300);
    }
    {
      const BoundaryCheck bc = check_boundary(bounds, 2000, 300);
      assert(bc.crossed);
      assert(bc.direction == Direction::Right);
      assert(bc.clamped_x == 1920);
      assert(bc.perp_pos == 300);
    }
    {
      const BoundaryCheck bc = check_boundary(bounds, 400, -50);
      assert(bc.crossed);
      assert(bc.direction == Direction::Up);
      assert(bc.clamped_y == 0);
      assert(bc.perp_pos == 400);
    }
    {
      const BoundaryCheck bc = check_boundary(bounds, 400, 2000);
      assert(bc.crossed);
      assert(bc.direction == Direction::Down);
      assert(bc.clamped_y == 1080);
      assert(bc.perp_pos == 400);
    }
  }

  // invert_entry: both clusters anchored at (0,0) -> inverse offset is a
  // simple negation, direction flips.
  {
    const ClusterBounds master{0, 1920, 0, 1080};
    const ClusterBounds peer{0, 1920, 0, 1080};
    const ArrangementEntry entry{42, Direction::Right, 100};
    const ArrangementEntry inv = invert_entry(entry, master, peer);
    assert(inv.direction == Direction::Left);
    assert(inv.offset == -100);
  }

  // invert_entry: round-trips back to the original when applied twice.
  {
    const ClusterBounds master{0, 1920, 0, 1080};
    const ClusterBounds peer{0, 1920, 0, 1080};
    const ArrangementEntry entry{42, Direction::Down, -50};
    const ArrangementEntry inv = invert_entry(entry, master, peer);
    assert(inv.direction == Direction::Up);
    assert(inv.offset == 50);
    const ArrangementEntry back = invert_entry(inv, peer, master);
    assert(back.direction == entry.direction);
    assert(back.offset == entry.offset);
  }

  // invert_entry: non-zero cluster origins (e.g. a peer whose monitor union
  // doesn't start at (0,0)) shift the inverse offset by the correction term
  // rather than a plain negation.
  {
    const ClusterBounds master{0, 1920, 0, 1080};
    const ClusterBounds peer{500, 2420, 200, 1280};  // peer's own local origin is (500, 200)
    const ArrangementEntry entry{7, Direction::Right, 300};
    const ArrangementEntry inv = invert_entry(entry, master, peer);
    assert(inv.direction == Direction::Left);
    assert(inv.offset == 200 + 0 - 300);  // peer.min_y + master.min_y - offset

    const ClusterBounds peer2{500, 2420, 200, 1280};
    const ArrangementEntry entry2{7, Direction::Up, 300};
    const ArrangementEntry inv2 = invert_entry(entry2, master, peer2);
    assert(inv2.direction == Direction::Down);
    assert(inv2.offset == 500 + 0 - 300);  // peer.min_x + master.min_x - offset
  }

  return 0;
}
