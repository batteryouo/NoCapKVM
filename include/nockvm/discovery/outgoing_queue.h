#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

namespace nockvm::discovery {

struct OutgoingMessage {
  uint8_t type = 0;
  std::vector<uint8_t> payload;
};

// Messages waiting for one connection's writer thread. Ordered messages
// (input, control) always go out before the single bulk slot (clipboard),
// so a large bulk payload delays them by at most the one frame already
// being written. Thread-safe; push*() never blocks on the network.
class OutgoingQueue {
public:
  enum class PushResult { Ok, Closed, Overflow };

  explicit OutgoingQueue(size_t max_ordered = 32768) : max_ordered_(max_ordered) {}

  // Starts accepting pushes, discarding anything left from a previous open.
  void open();

  // Stops accepting pushes and makes pop() return nullopt once woken.
  void close();

  // Appends to the ordered queue. Overflow means the writer has fallen
  // max_ordered messages behind; the message is not queued.
  PushResult push(uint8_t type, const uint8_t* payload, size_t len);

  // Like push(), but if the newest queued ordered message has the same
  // type, overwrites it instead of appending. For state where only the
  // latest value matters (absolute pointer position), so a backlog does
  // not replay stale values once the writer catches up.
  PushResult push_latest(uint8_t type, const uint8_t* payload, size_t len);

  // Replaces any bulk message that has not started sending yet.
  PushResult push_bulk(uint8_t type, const uint8_t* payload, size_t len);

  // Blocks for the next message: ordered first, then bulk. Returns nullopt
  // once closed.
  std::optional<OutgoingMessage> pop();

private:
  const size_t max_ordered_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool open_ = false;
  std::deque<OutgoingMessage> ordered_;
  std::optional<OutgoingMessage> bulk_;
};

}  // namespace nockvm::discovery
