#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace nockvm::audio {

// Re-slices a byte stream into fixed-size packets. The capture backend, not
// the requested period, decides how many frames each callback delivers (a
// PipeWire quantum, for example), so packets must not follow callback
// boundaries: an oversized packet exceeds the receiver's buffer, and the
// jitter buffer assumes every packet covers the same duration.
// A packet_bytes of zero emits nothing. Not thread-safe; allocates only on construction.
class PacketChunker {
public:
  explicit PacketChunker(size_t packet_bytes) : packet_bytes_(packet_bytes) { pending_.reserve(packet_bytes); }

  // Calls emit(data, packet_bytes) for each completed packet, in stream
  // order, and keeps any remainder for the next call.
  template <typename Emit>
  void feed(const uint8_t* data, size_t len, Emit&& emit) {
    if (packet_bytes_ == 0) return;
    if (!pending_.empty()) {
      const size_t take = std::min(len, packet_bytes_ - pending_.size());
      pending_.insert(pending_.end(), data, data + take);
      data += take;
      len -= take;
      if (pending_.size() < packet_bytes_) return;
      emit(pending_.data(), packet_bytes_);
      pending_.clear();
    }
    for (; len >= packet_bytes_; data += packet_bytes_, len -= packet_bytes_) emit(data, packet_bytes_);
    pending_.insert(pending_.end(), data, data + len);
  }

  size_t pending_bytes() const { return pending_.size(); }

private:
  size_t packet_bytes_;
  std::vector<uint8_t> pending_;
};

}  // namespace nockvm::audio
