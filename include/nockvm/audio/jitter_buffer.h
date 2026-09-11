#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <map>
#include <optional>
#include <vector>

namespace nockvm::audio {

// Reorders/paces incoming audio frames for playback, tolerating loss and
// reordering rather than blocking on it (a dropped or late frame should
// become a moment of silence, not accumulating delay). Thread-safe: fed
// from a network-receive thread via push(), drained from a real-time audio
// callback thread via pop().
class JitterBuffer {
public:
  // target_depth: how many frames to buffer before playback starts.
  // max_depth: hard ceiling on how many may ever be waiting at once.
  JitterBuffer(size_t target_depth, size_t max_depth);

  // Inserts a decoded frame at the given sequence number. Out-of-order and
  // duplicate-safe; silently dropped if it arrives after playback has
  // already moved past that sequence number. Also bounded: once more than
  // max_depth frames are waiting, the oldest are discarded (and playback
  // skipped past them) rather than allowed to pile up -- see max_depth
  // below.
  void push(uint32_t seq, std::vector<uint8_t> frame);

  // How many frames are waiting to be played. Purely diagnostic: hovering
  // near target_depth is healthy; pop() actively pulls it back down
  // whenever it drifts too far above that (see pop()), so a session sitting
  // well above target_depth for its entire duration without ever
  // approaching max_depth points at something upstream (repeatedly)
  // producing bursts, not at this buffer failing to keep up. Parked at
  // max_depth means push() itself is discarding frames outright because
  // pop() isn't draining them at all.
  size_t depth() const;

  // The max_depth passed to the constructor -- exposed so callers (e.g.
  // telemetry's periodic summary) can report depth alongside the ceiling
  // it's measured against without duplicating the value.
  size_t capacity() const { return max_depth_; }

  // Cumulative count of pop() calls where the expected next sequence
  // number hadn't arrived in time -- see pop(). This is the basis for the
  // UI's "audio playout misses" metric: an estimate, not exact network
  // packet loss, since it can't distinguish "lost in transit" from
  // "arrived after its playback slot had already passed". Atomic and
  // I/O-free: safe to poll from another thread without synchronizing with
  // push()/pop().
  uint64_t playout_misses() const { return playout_misses_.load(std::memory_order_relaxed); }

  // Cumulative count of pop() calls that successfully returned a frame --
  // the denominator playout_misses() is naturally measured against.
  uint64_t frames_played() const { return frames_played_.load(std::memory_order_relaxed); }

  // Cumulative count of "at cap" episodes since construction, debounced by
  // a recovery threshold (see recovery_threshold_) so a sustained overflow
  // counts as one episode rather than one per dropped packet. Atomic and
  // I/O-free: safe to poll from another thread (e.g. a periodic reporter)
  // without synchronizing with push()/pop().
  uint64_t cap_episodes() const { return cap_episodes_.load(std::memory_order_relaxed); }

  // Returns the next frame to play once enough have buffered up
  // (target_depth reached); nullopt means "not ready yet" or "that
  // sequence number never arrived" — caller should play silence either
  // way rather than blocking. If depth() has drifted more than a small
  // margin above target_depth (typically a burst of already-buffered audio
  // handed over all at once on a capture device's first callback), this
  // fast-forwards past the backlog first so depth() drops back toward
  // target_depth in this one call rather than staying elevated for the
  // rest of the session.
  std::optional<std::vector<uint8_t>> pop();

private:
  // Clears at_cap_ once buffer_.size() has drained to at or below
  // recovery_threshold_. Callable from both push() and pop(); caller must
  // hold mutex_.
  void clear_at_cap_if_recovered();

  mutable std::mutex mutex_;
  std::map<uint32_t, std::vector<uint8_t>> buffer_;
  size_t target_depth_;
  // pop() drains at the playback device's rate and push() fills at the
  // sender's; two machines' audio clocks are never exactly equal, and
  // nothing else in here ever discards a backlog -- so any sustained
  // excess on the send side would otherwise accumulate in buffer_
  // forever. An unbounded container fed straight off the network is not a
  // shape this should ever have, whatever the rates happen to be.
  size_t max_depth_;
  // Hysteresis floor for cap_episodes(): at_cap_ only clears once depth()
  // drops to at or below this. Computed once from max_depth_.
  size_t recovery_threshold_;
  bool started_ = false;
  uint32_t next_seq_ = 0;
  std::atomic<bool> at_cap_{false};
  std::atomic<uint64_t> cap_episodes_{0};
  std::atomic<uint64_t> playout_misses_{0};
  std::atomic<uint64_t> frames_played_{0};
  // Once playback's own next_seq_ races ahead of what the sender has
  // actually gotten to -- inevitable after any sufficiently long stall
  // (a burst of loss, or just sender/receiver clock drift accumulating
  // over a long session) -- every subsequent packet looks "too late" and
  // gets rejected forever, since next_seq_ only ever increases. Counting
  // consecutive misses and resetting once it's clearly not just ordinary
  // jitter/loss lets a fresh run of arriving packets re-synchronize
  // instead of playback staying silent for the rest of the connection.
  size_t consecutive_misses_ = 0;
};

}  // namespace nockvm::audio
