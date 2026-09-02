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

}  // namespace

JitterBuffer::JitterBuffer(size_t target_depth) : target_depth_(target_depth) {}

void JitterBuffer::push(uint32_t seq, std::vector<uint8_t> frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_ && seq < next_seq_) return;  // too late to matter, drop
  buffer_[seq] = std::move(frame);
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
    }
    return std::nullopt;
  }

  consecutive_misses_ = 0;
  std::vector<uint8_t> frame = std::move(it->second);
  buffer_.erase(it);
  return frame;
}

}  // namespace nockvm::audio
