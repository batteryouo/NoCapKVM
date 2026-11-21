#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include "nockvm/audio/format.h"

namespace nockvm::app {

// One side's "Send audio" checkbox plus capture format.
struct AudioSettings {
  bool send_enabled = true;
  audio::AudioFormat format;

  bool operator==(const AudioSettings& other) const {
    return send_enabled == other.send_enabled && format == other.format;
  }
  bool operator!=(const AudioSettings& other) const { return !(*this == other); }
};

// A Master request as sent on the wire (kMsgAudioControl).
struct AudioControl {
  AudioSettings settings;
  uint32_t id = 0;  // increasing and nonzero
};

// Master-side view of Slave's audio settings. Slave owns the settings: it
// applies Master's requests in order and reports what it actually runs,
// together with the id of the last request it applied. The Master UI
// normally shows Slave's latest report. After a Master click the UI shows
// the clicked value until a report acknowledges that request's id (or a
// later one), so the control does not flick back while the request is in
// flight. Matching on the id, not on the value, matters: after a quick
// "off, then on", an old "on" report must not count as confirming the
// second click. If no acknowledgement arrives within `timeout` (an older
// Slave without ids, or a lost connection), the UI falls back to the report.
// Pure state machine: no clock reads, no I/O. Callers pass explicit times.
class AudioRequest {
public:
  explicit AudioRequest(std::chrono::steady_clock::duration timeout) : timeout_(timeout) {}

  // Records a Master click. take_unsent() returns it once.
  void request(const AudioSettings& wanted, std::chrono::steady_clock::time_point now) {
    pending_ = wanted;
    pending_id_ = ++last_id_;
    if (pending_id_ == 0) pending_id_ = ++last_id_;  // 0 means "no request" on the wire
    pending_since_ = now;
    unsent_ = true;
  }

  // The request still to be sent to Slave, if any. Returns it only once.
  std::optional<AudioControl> take_unsent() {
    if (!unsent_ || !pending_) return std::nullopt;
    unsent_ = false;
    return AudioControl{*pending_, pending_id_};
  }

  // What the Master UI shows, given Slave's latest report and the request
  // id that report reflects. Clears the pending request once acknowledged
  // or timed out.
  AudioSettings shown(const AudioSettings& reported, uint32_t reported_request_id,
                      std::chrono::steady_clock::time_point now) {
    if (pending_ && (acknowledges(reported_request_id) || now - pending_since_ >= timeout_)) pending_.reset();
    return pending_ ? *pending_ : reported;
  }

  bool pending() const { return pending_.has_value(); }

  // Forgets any request. Call on disconnect: a new connection starts from
  // Slave's own settings. Ids keep increasing across connections.
  void reset() {
    pending_.reset();
    unsent_ = false;
  }

private:
  // Wrap-safe "reported id is this request or a later one". Slave reports
  // 0 until it applies a request on this connection.
  bool acknowledges(uint32_t reported_request_id) const {
    return reported_request_id != 0 && static_cast<int32_t>(reported_request_id - pending_id_) >= 0;
  }

  std::chrono::steady_clock::duration timeout_;
  std::optional<AudioSettings> pending_;
  uint32_t pending_id_ = 0;
  uint32_t last_id_ = 0;
  std::chrono::steady_clock::time_point pending_since_{};
  bool unsent_ = false;
};

}  // namespace nockvm::app
