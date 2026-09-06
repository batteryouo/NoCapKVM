#include <cassert>
#include "nockvm/audio/jitter_buffer.h"

using namespace nockvm::audio;

int main() {
  // Fills to target_depth before producing anything, then plays back in order.
  {
    JitterBuffer buf(3, 100);
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
    JitterBuffer buf(2, 100);
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
    JitterBuffer buf(2, 100);
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
    JitterBuffer buf(1, 100);
    buf.push(0, {10});
    assert(buf.pop().has_value());  // starts, plays seq 0, next_seq_ is now 1
    buf.push(0, {200});             // too late
    assert(!buf.pop().has_value());  // seq 1 still never arrived
  }

  // Duplicate pushes for the same not-yet-played sequence overwrite
  // harmlessly rather than erroring or duplicating playback.
  {
    JitterBuffer buf(1, 100);
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
    JitterBuffer buf(1, 100);
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

  // A sender outrunning playback (the normal case whenever two machines'
  // audio clocks differ at all, since nothing else here ever discards a
  // backlog) must not grow the buffer without limit.
  {
    JitterBuffer buf(2, 8);
    for (uint32_t seq = 0; seq < 1000; ++seq) {
      buf.push(seq, {static_cast<uint8_t>(seq)});
      assert(buf.depth() <= 8);
    }
    assert(buf.depth() == 8);

    // What survived is the newest run, and playback resumes from the oldest
    // of those rather than waiting forever on sequence numbers that were
    // dropped to stay within the cap.
    const auto f = buf.pop();
    assert(f.has_value() && (*f)[0] == static_cast<uint8_t>(992));
    const auto g = buf.pop();
    assert(g.has_value() && (*g)[0] == static_cast<uint8_t>(993));
  }

  // The cap also applies once playback has started, and skipping past
  // dropped frames must not leave pop() stuck asking for one of them.
  {
    JitterBuffer buf(1, 4);
    buf.push(0, {0});
    assert(buf.pop().has_value());  // started; next_seq_ == 1
    for (uint32_t seq = 1; seq <= 20; ++seq) buf.push(seq, {static_cast<uint8_t>(seq)});
    assert(buf.depth() == 4);
    const auto f = buf.pop();
    assert(f.has_value() && (*f)[0] == 17);
    assert(buf.depth() == 3);
  }

  // max_depth below target_depth would deadlock playback (the fill
  // threshold could never be reached); it's raised to target_depth instead.
  {
    JitterBuffer buf(3, 1);
    buf.push(0, {1});
    buf.push(1, {2});
    buf.push(2, {3});
    assert(buf.depth() == 3);
    assert(buf.pop().has_value());
  }

  return 0;
}
