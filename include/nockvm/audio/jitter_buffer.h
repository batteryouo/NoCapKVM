#pragma once
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
  bool started_ = false;
  uint32_t next_seq_ = 0;
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
