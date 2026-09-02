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
    buf.push(0, {200});             // too late
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

  // After a sustained stall (next_seq_ racing far ahead of anything
  // arriving -- a burst of loss, or clock drift over a long session), the
  // buffer resyncs instead of rejecting every future packet forever: a
  // packet whose sequence number would have looked "long past" under the
  // old position still gets accepted and played once resync kicks in.
  {
    JitterBuffer buf(1);
    buf.push(0, {10});
    assert(buf.pop().has_value());  // next_seq_ is now 1

    bool resynced = false;
    for (int i = 0; i < 100; ++i) {
      if (!buf.pop().has_value()) continue;
      resynced = true;
      break;
    }
    assert(!resynced);  // nothing was ever pushed in this loop -- always silence

    // Long before this session's real sequence numbers would ever repeat,
    // but well below whatever next_seq_ raced up to during the stall --
    // exactly the case that used to be rejected forever.
    buf.push(5, {50});
    const auto f = buf.pop();
    assert(f.has_value() && (*f)[0] == 50);
  }

  return 0;
}
