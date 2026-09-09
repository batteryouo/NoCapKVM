#include "nockvm/audio/jitter_buffer.h"
#include <algorithm>
#include "nockvm/telemetry/counters.h"

namespace nockvm::audio {
namespace {

// ~200ms of continuous silence (at 5ms/packet) before giving up on the
// current sequence position and letting a fresh run of arrivals
// re-synchronize. Long enough that ordinary single/double-packet loss --
// completely normal, and self-heals on its own the very next packet --
// never triggers it; short enough that a genuine stall doesn't leave
// playback silent for the rest of the connection.
constexpr size_t kMaxConsecutiveMisses = 40;

// cap_episodes()'s hysteresis margin: 10% of max_depth_ (at least 1),
// below the ceiling. depth() has to drain back down that far, not just
// off the exact ceiling by one packet, before a fresh episode can start.
size_t compute_recovery_threshold(size_t max_depth) {
  const size_t margin = std::max<size_t>(1, max_depth / 10);
  return margin < max_depth ? max_depth - margin : 0;
}

}  // namespace

JitterBuffer::JitterBuffer(size_t target_depth, size_t max_depth)
    : target_depth_(target_depth), max_depth_(max_depth < target_depth ? target_depth : max_depth),
      recovery_threshold_(compute_recovery_threshold(max_depth_)) {}

void JitterBuffer::push(uint32_t seq, std::vector<uint8_t> frame) {
  size_t dropped_count = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_ && seq < next_seq_) return;  // too late to matter, drop
    buffer_[seq] = std::move(frame);

    // Over the ceiling: throw away the oldest and skip playback past them.
    // Being this far behind means those frames were going to be heard late
    // or not at all anyway, so dropping them costs a moment of audio and
    // buys a bounded buffer -- the same trade this class already makes for
    // ordinary loss and reordering. Without it, a sender even slightly
    // faster than the playback device (which is the normal case between
    // two machines' independent audio clocks) grows this map without
    // limit for as long as the connection lasts.
    while (buffer_.size() > max_depth_) {
      const auto oldest = buffer_.begin();
      if (started_ && oldest->first >= next_seq_) next_seq_ = oldest->first + 1;
      buffer_.erase(oldest);
      ++dropped_count;
    }
    if (dropped_count > 0) {
      if (!at_cap_.load(std::memory_order_relaxed)) {
        at_cap_.store(true, std::memory_order_relaxed);
        cap_episodes_.fetch_add(1, std::memory_order_relaxed);
      }
    } else {
      clear_at_cap_if_recovered();
    }
  }

  // Atomic counter increment only -- no logging or other I/O -- so this
  // stays safe to call from a real-time or network thread.
  if (dropped_count > 0) {
    telemetry::counters().audio_packets_dropped_cap.fetch_add(dropped_count, std::memory_order_relaxed);
  }
}

size_t JitterBuffer::depth() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return buffer_.size();
}

void JitterBuffer::clear_at_cap_if_recovered() {
  if (at_cap_.load(std::memory_order_relaxed) && buffer_.size() <= recovery_threshold_) {
    at_cap_.store(false, std::memory_order_relaxed);
  }
}

std::optional<std::vector<uint8_t>> JitterBuffer::pop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!started_) {
    if (buffer_.size() < target_depth_) return std::nullopt;
    started_ = true;
    next_seq_ = buffer_.begin()->first;
    consecutive_misses_ = 0;
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
      clear_at_cap_if_recovered();
    }
    return std::nullopt;
  }

  consecutive_misses_ = 0;
  std::vector<uint8_t> frame = std::move(it->second);
  buffer_.erase(it);
  clear_at_cap_if_recovered();
  return frame;
}

}  // namespace nockvm::audio
