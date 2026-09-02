#include <cassert>
#include "nockvm/audio/jitter_buffer.h"

using namespace nockvm::audio;

int main() {
  // Fills to target_depth before producing anything, then plays back in order.
  {
    JitterBuffer buf(3);
    assert(!buf.pop().has_value());
    buf.push(0, {1});
    assert(!buf.pop().has_value());
    buf.push(1, {2});
    assert(!buf.pop().has_value());
    buf.push(2, {3});

    const auto f0 = buf.pop();
    assert(f0.has_value() && f0->size() == 1 && (*f0)[0] == 1);
    const auto f1 = buf.pop();
    assert(f1.has_value() && (*f1)[0] == 2);
  }

  // Out-of-order arrival still plays back in sequence order.
  {
    JitterBuffer buf(2);
    buf.push(1, {20});
    buf.push(0, {10});
    const auto f0 = buf.pop();
    assert(f0.has_value() && (*f0)[0] == 10);
    const auto f1 = buf.pop();
    assert(f1.has_value() && (*f1)[0] == 20);
  }

  // A missing sequence number produces silence (nullopt) once its turn
  // comes, but playback keeps advancing rather than stalling on it.
  {
    JitterBuffer buf(2);
    buf.push(0, {10});
    buf.push(2, {30});  // seq 1 never arrives
    assert(buf.pop().has_value());  // seq 0
    assert(!buf.pop().has_value());  // seq 1 missing -> silence
    const auto f2 = buf.pop();
    assert(f2.has_value() && (*f2)[0] == 30);
  }

  // A packet that arrives after playback has already moved past its
  // sequence number is dropped rather than resurrected out of order.
  {
    JitterBuffer buf(1);
    buf.push(0, {10});
    assert(buf.pop().has_value());  // starts, plays seq 0, next_seq_ is now 1
    buf.push(0, {999});             // too late
    assert(!buf.pop().has_value());  // seq 1 still never arrived
  }

  // Duplicate pushes for the same not-yet-played sequence overwrite
  // harmlessly rather than erroring or duplicating playback.
  {
    JitterBuffer buf(1);
    buf.push(0, {10});
    buf.push(0, {11});
    const auto f0 = buf.pop();
    assert(f0.has_value() && (*f0)[0] == 11);
  }

  return 0;
}
