#include "nockvm/audio/jitter_buffer.h"

namespace nockvm::audio {
namespace {

// ~200ms of continuous silence (at 5ms/packet) before giving up on the
// current sequence position and letting a fresh run of arrivals
// re-synchronize. Long enough that ordinary single/double-packet loss --
// completely normal, and self-heals on its own the very next packet --
// never triggers it; short enough that a genuine stall doesn't leave
// playback silent for the rest of the connection.
constexpr size_t kMaxConsecutiveMisses = 40;

// How far above target_depth_ the buffer is allowed to sit before pop()
// fast-forwards it back down. Ordinary jitter/reordering can legitimately
// push depth a little past target without anything being wrong, so this
// needs slack -- but nothing between target_depth_ and max_depth_ was ever
// actively pulling depth back toward target_depth_ once something (commonly
// a one-time burst of already-buffered audio handed over on a capture
// device's very first callback) pushed it up there, so a session could sit
// hundreds of milliseconds behind real time for its entire duration without
// ever reaching max_depth_ -- observed directly: audio buffer parked at
// 100-170 packets (0.5-0.85s) for an entire session, never climbing toward
// the 200 ceiling and never draining back toward target_depth_ either.
constexpr size_t kCatchUpMargin = 10;

}  // namespace

JitterBuffer::JitterBuffer(size_t target_depth, size_t max_depth)
    : target_depth_(target_depth), max_depth_(max_depth < target_depth ? target_depth : max_depth) {}

void JitterBuffer::push(uint32_t seq, std::vector<uint8_t> frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_ && seq < next_seq_) return;  // too late to matter, drop
  buffer_[seq] = std::move(frame);

  // Over the ceiling: throw away the oldest and skip playback past them.
  // Being this far behind means those frames were going to be heard late
  // or not at all anyway, so dropping them costs a moment of audio and
  // buys a bounded buffer -- the same trade this class already makes for
  // ordinary loss and reordering. Without it, a sender even slightly
  // faster than the playback device (which is the normal case between two
  // machines' independent audio clocks) grows this map without limit for
  // as long as the connection lasts.
  while (buffer_.size() > max_depth_) {
    const auto oldest = buffer_.begin();
    if (started_ && oldest->first >= next_seq_) next_seq_ = oldest->first + 1;
    buffer_.erase(oldest);
  }
}

size_t JitterBuffer::depth() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return buffer_.size();
}

std::optional<std::vector<uint8_t>> JitterBuffer::pop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!started_) {
    if (buffer_.size() < target_depth_) return std::nullopt;
    started_ = true;
    next_seq_ = buffer_.begin()->first;
    consecutive_misses_ = 0;
  }

  // Catch up before playing anything: if a burst left far more than
  // target_depth_ waiting, jump next_seq_ straight to what's left rather
  // than working through the backlog one packet per callback -- the latter
  // would just keep depth() permanently elevated instead of shrinking it,
  // since normal playback drains at the same rate frames keep arriving.
  // One-shot rather than gradual on purpose: whatever gets skipped here was
  // going to be heard late no matter what, so there's nothing to gain by
  // spreading the skip out.
  while (buffer_.size() > target_depth_ + kCatchUpMargin) {
    const auto oldest = buffer_.begin();
    next_seq_ = oldest->first + 1;
    buffer_.erase(oldest);
  }

  const auto it = buffer_.find(next_seq_);
  ++next_seq_;

  if (it == buffer_.end()) {
    // Lost or not yet arrived -- play silence, keep pace, but if this has
    // gone on too long, next_seq_ has likely raced ahead of wherever the
    // sender actually is (a stall, or accumulated clock drift over a long
    // session): every future packet would otherwise look "too late"
    // forever, since next_seq_ only ever increases. Drop back into the
    // initial fill state so whatever arrives next gets a fresh start
    // instead of being silently rejected for good.
    if (++consecutive_misses_ >= kMaxConsecutiveMisses) {
      started_ = false;
      consecutive_misses_ = 0;
      buffer_.clear();
    }
    return std::nullopt;
  }

  consecutive_misses_ = 0;
  std::vector<uint8_t> frame = std::move(it->second);
  buffer_.erase(it);
  return frame;
}

}  // namespace nockvm::audio
