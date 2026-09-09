#pragma once
#include <chrono>

namespace nockvm::app {

// Gates retries of a failing start-like operation and log-once reporting
// of its failure, independent of the caller's own poll rate. Pure state
// machine -- no clock reads, no I/O -- callers pass explicit time points.
class RetryBackoff {
public:
  explicit RetryBackoff(std::chrono::steady_clock::duration backoff) : backoff_(backoff) {}

  // Whether a new attempt should be made at time `now`.
  bool should_attempt(std::chrono::steady_clock::time_point now) const {
    return !has_retry_after_ || now >= retry_after_;
  }

  // Records a failed attempt: arms the backoff and returns true only for
  // the first failure since the last on_success()/reset() -- callers
  // should log a failure only when this returns true.
  bool on_failure(std::chrono::steady_clock::time_point now) {
    retry_after_ = now + backoff_;
    has_retry_after_ = true;
    if (failure_logged_) return false;
    failure_logged_ = true;
    return true;
  }

  // Records a successful attempt: clears the backoff and re-arms
  // on_failure() to report the next episode's first failure again.
  void on_success() {
    has_retry_after_ = false;
    failure_logged_ = false;
  }

  // Resets to the initial state, so a later attempt starts a fresh
  // failure episode instead of inheriting a stale one.
  void reset() {
    has_retry_after_ = false;
    failure_logged_ = false;
  }

private:
  std::chrono::steady_clock::duration backoff_;
  std::chrono::steady_clock::time_point retry_after_{};
  bool has_retry_after_ = false;
  bool failure_logged_ = false;
};

}  // namespace nockvm::app
