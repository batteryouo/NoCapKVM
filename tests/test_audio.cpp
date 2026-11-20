#include <cassert>
#include <vector>
#include "nockvm/audio/format.h"
#include "nockvm/audio/jitter_buffer.h"
#include "nockvm/audio/level.h"
#include "nockvm/audio/packet_chunker.h"
#include "nockvm/telemetry/counters.h"

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

    // A low sequence number restarts playback after a prolonged stall.
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

  // Frames below the hard limit retain their sequence order.
  {
    JitterBuffer buf(3, 200);
    for (uint32_t seq = 0; seq < 50; ++seq) buf.push(seq, {static_cast<uint8_t>(seq)});
    assert(buf.depth() == 50);

    const auto f = buf.pop();
    assert(f.has_value() && (*f)[0] == 0);
    assert(buf.depth() == 49);

    const auto g = buf.pop();
    assert(g.has_value() && (*g)[0] == 1);
  }

  // capacity() reports the max_depth actually in effect, including the
  // below-target_depth clamp exercised above.
  {
    JitterBuffer buf(3, 100);
    assert(buf.capacity() == 100);
    JitterBuffer clamped(3, 1);
    assert(clamped.capacity() == 3);
  }

  // Pushing past the cap increments the shared telemetry counter, so the
  // periodic summary can report how many packets the cap has discarded.
  {
    auto& counters = nockvm::telemetry::counters();
    const uint64_t before = counters.audio_packets_dropped_cap.load();
    JitterBuffer buf(2, 4);
    for (uint32_t seq = 0; seq < 10; ++seq) buf.push(seq, {static_cast<uint8_t>(seq)});
    assert(counters.audio_packets_dropped_cap.load() > before);
  }

  // A sustained overflow counts as one episode, not one per dropped
  // packet.
  {
    JitterBuffer buf(2, 20);
    for (uint32_t seq = 0; seq < 100; ++seq) buf.push(seq, {static_cast<uint8_t>(seq)});
    assert(buf.depth() == 20);
    assert(buf.cap_episodes() == 1);

    // Reaching the ceiling without draining below the recovery threshold
    // (18 for this buffer) does not clear the cap state.
    assert(buf.pop().has_value());
    buf.push(200, {1});
    assert(buf.depth() == 20);
    assert(buf.cap_episodes() == 1);

    // Draining below the recovery threshold clears the cap state; the
    // next overflow counts as a new episode.
    for (int i = 0; i < 5; ++i) assert(buf.pop().has_value());
    assert(buf.depth() == 15);
    buf.push(201, {1});
    assert(buf.depth() == 16);
    assert(buf.cap_episodes() == 1);

    for (uint32_t seq = 202; seq < 212; ++seq) buf.push(seq, {static_cast<uint8_t>(seq)});
    assert(buf.depth() == 20);
    assert(buf.cap_episodes() == 2);
  }

  // Draining via pop() alone, with no intervening push(), also clears the
  // cap state.
  {
    JitterBuffer buf(2, 20);
    for (uint32_t seq = 0; seq < 100; ++seq) buf.push(seq, {static_cast<uint8_t>(seq)});
    assert(buf.cap_episodes() == 1);

    for (int i = 0; i < 5; ++i) assert(buf.pop().has_value());
    assert(buf.depth() == 15);

    for (uint32_t seq = 300; seq < 320; ++seq) buf.push(seq, {static_cast<uint8_t>(seq)});
    assert(buf.depth() == 20);
    assert(buf.cap_episodes() == 2);
  }

  // playout_misses() counts pop() calls where the expected sequence number
  // hadn't arrived, but not the initial fill-up wait before playback starts.
  {
    JitterBuffer buf(2, 100);
    assert(!buf.pop().has_value());  // still filling -- not a miss
    assert(buf.playout_misses() == 0);

    buf.push(0, {10});
    buf.push(2, {30});  // seq 1 never arrives
    assert(buf.pop().has_value());   // seq 0 plays
    assert(buf.frames_played() == 1);
    assert(!buf.pop().has_value());  // seq 1 missing -> counted as a miss
    assert(buf.playout_misses() == 1);
    const auto f2 = buf.pop();
    assert(f2.has_value() && (*f2)[0] == 30);
    assert(buf.frames_played() == 2);
    assert(buf.playout_misses() == 1);
  }

  // A late/dropped packet (arrives after playback already moved past its
  // sequence number) shows up as a playout miss once pop() reaches that
  // sequence number, since the frame is simply absent at that point.
  {
    JitterBuffer buf(1, 100);
    buf.push(0, {10});
    assert(buf.pop().has_value());   // starts, plays seq 0, next_seq_ is now 1
    buf.push(0, {200});              // too late, dropped by push()
    assert(!buf.pop().has_value());  // seq 1 still never arrived -> miss
    assert(buf.playout_misses() == 1);
  }

  // Callbacks larger than a packet are split, and the remainder joins the
  // next callback's bytes, so every packet has the same size and the stream
  // is preserved byte for byte.
  {
    PacketChunker chunker(4);
    std::vector<uint8_t> input(10);
    for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<uint8_t>(i);
    std::vector<std::vector<uint8_t>> packets;
    auto collect = [&](const uint8_t* data, size_t len) { packets.emplace_back(data, data + len); };

    chunker.feed(input.data(), 10, collect);
    assert(packets.size() == 2 && chunker.pending_bytes() == 2);
    const uint8_t tail[] = {10, 11, 12, 13, 14, 15};
    chunker.feed(tail, sizeof(tail), collect);
    assert(packets.size() == 4 && chunker.pending_bytes() == 0);
    for (size_t p = 0; p < packets.size(); ++p) {
      assert(packets[p].size() == 4);
      for (size_t i = 0; i < 4; ++i) assert(packets[p][i] == p * 4 + i);
    }
  }

  // Callbacks smaller than a packet accumulate until one is complete.
  {
    PacketChunker chunker(960);
    const std::vector<uint8_t> small(300, 7);
    size_t emitted = 0;
    auto count = [&](const uint8_t*, size_t len) {
      assert(len == 960);
      ++emitted;
    };
    for (int i = 0; i < 3; ++i) chunker.feed(small.data(), small.size(), count);
    assert(emitted == 0 && chunker.pending_bytes() == 900);
    chunker.feed(small.data(), small.size(), count);
    assert(emitted == 1 && chunker.pending_bytes() == 240);
  }

  // A 512-frame 16-bit stereo callback (2048 bytes) becomes 5ms packets
  // well under the receiver's 2048-byte buffer.
  {
    const AudioFormat format{48000, 16};
    PacketChunker chunker(format.bytes_per_packet());
    const std::vector<uint8_t> callback(512 * format.bytes_per_frame(), 0);
    size_t emitted = 0;
    chunker.feed(callback.data(), callback.size(), [&](const uint8_t*, size_t len) {
      assert(len == 960);
      ++emitted;
    });
    assert(emitted == 2 && chunker.pending_bytes() == 128);
  }

  // A zero sample rate (possible from an unvalidated network message) is
  // unsupported and would size packets at zero bytes; the chunker must
  // return rather than loop forever on the audio thread.
  {
    const AudioFormat zero_rate{0, 16};
    assert(!zero_rate.is_supported() && zero_rate.bytes_per_packet() == 0);
    const AudioFormat cd_rate{44100, 16}, deep{48000, 24}, low{24000, 8}, high{48000, 16};
    assert(!cd_rate.is_supported() && !deep.is_supported());
    assert(low.is_supported() && high.is_supported());

    PacketChunker chunker(zero_rate.bytes_per_packet());
    const uint8_t data[16] = {};
    bool emitted = false;
    chunker.feed(data, sizeof(data), [&](const uint8_t*, size_t) { emitted = true; });
    assert(!emitted && chunker.pending_bytes() == 0);
  }

  // peak_ppm: silence is 0 for both depths (8-bit silence is the 0x80
  // midpoint, not 0x00), full scale is 1000000, and 16-bit samples are
  // read little-endian and signed.
  {
    const uint8_t silent8[] = {0x80, 0x80, 0x80};
    assert(peak_ppm(silent8, sizeof(silent8), 8) == 0);
    const uint8_t full8[] = {0x80, 0x00, 0x80};
    assert(peak_ppm(full8, sizeof(full8), 8) == 1000000);

    const uint8_t silent16[] = {0, 0, 0, 0};
    assert(peak_ppm(silent16, sizeof(silent16), 16) == 0);
    const uint8_t full16[] = {0x00, 0x80, 0x01, 0x00};  // -32768, then 1
    assert(peak_ppm(full16, sizeof(full16), 16) == 1000000);
    const uint8_t half16[] = {0x00, 0x40};  // 16384
    assert(peak_ppm(half16, sizeof(half16), 16) == 500000);

    assert(peak_ppm(full16, sizeof(full16), 24) == 0);
  }

  return 0;
}
