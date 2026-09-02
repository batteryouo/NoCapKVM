#include "nockvm/audio/jitter_buffer.h"

namespace nockvm::audio {

JitterBuffer::JitterBuffer(size_t target_depth) : target_depth_(target_depth) {}

void JitterBuffer::push(uint32_t seq, std::vector<int16_t> frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_ && seq < next_seq_) return;  // too late to matter, drop
  buffer_[seq] = std::move(frame);
}

std::optional<std::vector<int16_t>> JitterBuffer::pop() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!started_) {
    if (buffer_.size() < target_depth_) return std::nullopt;
    started_ = true;
    next_seq_ = buffer_.begin()->first;
  }

  const auto it = buffer_.find(next_seq_);
  ++next_seq_;
  if (it == buffer_.end()) return std::nullopt;  // lost or not yet arrived -- play silence, keep pace

  std::vector<int16_t> frame = std::move(it->second);
  buffer_.erase(it);
  return frame;
}

}  // namespace nockvm::audio
