#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <deque>

namespace nockvm::app {

// Tracks the largest sampled buffer depth in a rolling time window.
class BufferDepthWindow {
public:
  explicit BufferDepthWindow(std::chrono::steady_clock::duration window = std::chrono::seconds(10))
      : window_(window) {}

  void record(std::chrono::steady_clock::time_point now, size_t depth) {
    if (!samples_.empty() && now - samples_.back().at > window_) {
      samples_.clear();
      has_first_sample_ = false;
      ready_ = false;
    }
    if (!has_first_sample_) {
      first_sample_at_ = now;
      has_first_sample_ = true;
    }
    samples_.push_back({now, depth});
    while (!samples_.empty() && now - samples_.front().at > window_) samples_.pop_front();
    ready_ = now - first_sample_at_ >= window_;
  }

  void reset() {
    samples_.clear();
    has_first_sample_ = false;
    ready_ = false;
  }

  bool ready() const { return ready_; }

  size_t maximum() const {
    size_t result = 0;
    for (const Sample& sample : samples_) result = std::max(result, sample.depth);
    return result;
  }

private:
  struct Sample {
    std::chrono::steady_clock::time_point at;
    size_t depth;
  };
  std::chrono::steady_clock::duration window_;
  std::deque<Sample> samples_;
  std::chrono::steady_clock::time_point first_sample_at_{};
  bool has_first_sample_ = false;
  bool ready_ = false;
};

}  // namespace nockvm::app
