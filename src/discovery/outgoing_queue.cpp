#include "nockvm/discovery/outgoing_queue.h"

namespace nockvm::discovery {

void OutgoingQueue::open() {
  std::lock_guard<std::mutex> lock(mutex_);
  ordered_.clear();
  bulk_.reset();
  open_ = true;
}

void OutgoingQueue::close() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
    ordered_.clear();
    bulk_.reset();
  }
  cv_.notify_all();
}

OutgoingQueue::PushResult OutgoingQueue::push(uint8_t type, const uint8_t* payload, size_t len) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return PushResult::Closed;
    if (ordered_.size() >= max_ordered_) return PushResult::Overflow;
    ordered_.push_back(OutgoingMessage{type, std::vector<uint8_t>(payload, payload + len)});
  }
  cv_.notify_one();
  return PushResult::Ok;
}

OutgoingQueue::PushResult OutgoingQueue::push_bulk(uint8_t type, const uint8_t* payload, size_t len) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return PushResult::Closed;
    bulk_ = OutgoingMessage{type, std::vector<uint8_t>(payload, payload + len)};
  }
  cv_.notify_one();
  return PushResult::Ok;
}

std::optional<OutgoingMessage> OutgoingQueue::pop() {
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return !open_ || !ordered_.empty() || bulk_.has_value(); });
  if (!open_) return std::nullopt;
  if (!ordered_.empty()) {
    OutgoingMessage msg = std::move(ordered_.front());
    ordered_.pop_front();
    return msg;
  }
  OutgoingMessage msg = std::move(*bulk_);
  bulk_.reset();
  return msg;
}

}  // namespace nockvm::discovery
