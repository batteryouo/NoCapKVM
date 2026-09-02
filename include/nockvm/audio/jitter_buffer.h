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
  explicit JitterBuffer(size_t target_depth);

  // Inserts a decoded frame at the given sequence number. Out-of-order and
  // duplicate-safe; silently dropped if it arrives after playback has
  // already moved past that sequence number.
  void push(uint32_t seq, std::vector<int16_t> frame);

  // Returns the next frame to play once enough have buffered up
  // (target_depth reached); nullopt means "not ready yet" or "that
  // sequence number never arrived" — caller should play silence either
  // way rather than blocking.
  std::optional<std::vector<int16_t>> pop();

private:
  std::mutex mutex_;
  std::map<uint32_t, std::vector<int16_t>> buffer_;
  size_t target_depth_;
  bool started_ = false;
  uint32_t next_seq_ = 0;
};

}  // namespace nockvm::audio
