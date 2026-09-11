#pragma once
#include <chrono>
#include <cstdint>
#include <deque>

namespace nockvm::app {

// Rolling window over a monotonically increasing pair of cumulative
// counters (JitterBuffer's playout_misses()/frames_played(), sampled once
// per second by the caller -- see audio_pump.cpp's pump_master()), used to
// report an audio playout-miss rate scoped to roughly the last `window`
// instead of the whole playback instance's lifetime. A gap between samples
// longer than `window` itself (e.g. the process was suspended, or the main
// loop stalled) restarts the window rather than reporting a delta that
// silently spans the whole gap under a "last `window`" label -- see
// record(). Pure state machine: no clock reads or I/O of its own --
// callers pass in an explicit time point with each sample, mirroring
// RetryBackoff's shape.
class AudioLossWindow {
public:
  explicit AudioLossWindow(std::chrono::steady_clock::duration window = std::chrono::seconds(60))
      : window_(window) {}

  // Records one sample of the current cumulative counters at `now`.
  //
  // If the gap since the last sample is itself longer than `window` (the
  // app was suspended, or the main loop stalled), the retained history no
  // longer reflects "the last `window`" at all -- reporting a delta across
  // it would silently span the whole gap under a "last `window`" label, so
  // it's discarded instead: the window restarts from this sample and needs
  // a fresh `window` of samples before reading() is ready again.
  //
  // Otherwise, old samples are evicted, but the front is only dropped once
  // the *next* sample (samples_[1]) is itself at or past the window
  // boundary -- i.e. the front stays as long as it's the newest sample old
  // enough to anchor the window. This matters when sampling is sparser
  // than usual but still within `window`: with only two samples,
  // samples_[1] is the one just pushed, whose age relative to itself is
  // always 0, so it never looks "past the boundary" and the older sample
  // is correctly kept as the reference point instead of being evicted down
  // to a single, zero-width sample.
  void record(std::chrono::steady_clock::time_point now, uint64_t misses, uint64_t played) {
    if (!samples_.empty() && now - samples_.back().at > window_) {
      samples_.clear();
      has_first_sample_ = false;
    }
    if (!has_first_sample_) {
      first_sample_at_ = now;
      has_first_sample_ = true;
    }
    samples_.push_back({now, misses, played});
    while (samples_.size() > 1 && now - samples_[1].at >= window_) samples_.pop_front();
    ready_ = now - first_sample_at_ >= window_;
  }

  // Clears all history -- call whenever the counters' source (AudioPlayback)
  // is reconstructed, the connection drops, or playback becomes
  // unavailable, so a stale/unrelated instance's counters are never
  // compared against a fresh one's.
  void reset() {
    samples_.clear();
    has_first_sample_ = false;
    ready_ = false;
  }

  struct Reading {
    // False until a full `window` has actually been observed since the
    // first record() following construction/reset() -- the UI shows
    // "measuring..." for that case rather than a rate from partial data.
    bool ready = false;
    uint64_t misses = 0;
    uint64_t attempts = 0;
  };

  // Derives the window's numerator/denominator from the oldest retained
  // sample and the most recently recorded one.
  Reading reading() const {
    Reading r;
    r.ready = ready_;
    if (!ready_ || samples_.empty()) return r;
    const Sample& oldest = samples_.front();
    const Sample& newest = samples_.back();
    r.misses = newest.misses >= oldest.misses ? newest.misses - oldest.misses : 0;
    const uint64_t played_delta = newest.played >= oldest.played ? newest.played - oldest.played : 0;
    r.attempts = r.misses + played_delta;
    return r;
  }

private:
  struct Sample {
    std::chrono::steady_clock::time_point at;
    uint64_t misses;
    uint64_t played;
  };
  std::chrono::steady_clock::duration window_;
  std::deque<Sample> samples_;
  std::chrono::steady_clock::time_point first_sample_at_{};
  bool has_first_sample_ = false;
  bool ready_ = false;
};

}  // namespace nockvm::app
