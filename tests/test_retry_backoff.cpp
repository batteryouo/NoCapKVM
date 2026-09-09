#include <cassert>
#include <chrono>
#include "retry_backoff.h"

using namespace nockvm::app;
using namespace std::chrono_literals;

int main() {
  // A fresh gate always allows an immediate first attempt.
  {
    RetryBackoff gate(2s);
    const auto t0 = std::chrono::steady_clock::time_point(100s);
    assert(gate.should_attempt(t0));
  }

  // A failure arms the backoff -- no further attempts until it elapses --
  // and only the first failure in a streak reports true (log-once); a
  // second failure right at the retry boundary re-arms the backoff but
  // does not report true again, since it's still the same episode.
  {
    RetryBackoff gate(2s);
    const auto t0 = std::chrono::steady_clock::time_point(100s);
    assert(gate.should_attempt(t0));
    assert(gate.on_failure(t0));  // first failure of the episode -- caller should log
    assert(!gate.should_attempt(t0 + 500ms));
    assert(!gate.should_attempt(t0 + 1999ms));
    assert(gate.should_attempt(t0 + 2000ms));  // backoff elapsed

    assert(!gate.on_failure(t0 + 2000ms));  // same episode -- caller should NOT log again
    assert(!gate.should_attempt(t0 + 2000ms + 1999ms));
    assert(gate.should_attempt(t0 + 2000ms + 2000ms));
  }

  // on_success() clears the backoff and starts a fresh episode: the next
  // failure reports true again.
  {
    RetryBackoff gate(2s);
    const auto t0 = std::chrono::steady_clock::time_point(0s);
    assert(gate.on_failure(t0));
    gate.on_success();
    assert(gate.should_attempt(t0));  // no backoff left after a success
    assert(gate.on_failure(t0));      // new episode -- reports true again
  }

  // reset() behaves like a brand-new gate regardless of prior failures --
  // used when the surrounding context (e.g. the connection) goes away.
  {
    RetryBackoff gate(2s);
    const auto t0 = std::chrono::steady_clock::time_point(0s);
    assert(gate.on_failure(t0));
    assert(!gate.should_attempt(t0 + 1s));
    gate.reset();
    assert(gate.should_attempt(t0 + 1s));
    assert(gate.on_failure(t0 + 1s));  // fresh episode again after reset()
  }

  // Under a tight polling loop, a persistent failure still logs at most
  // once and retries stay bounded by the backoff.
  {
    RetryBackoff gate(2s);
    auto t = std::chrono::steady_clock::time_point(0s);
    int logged = 0;
    int attempted = 0;
    for (int i = 0; i < 1000; ++i) {
      t += 16ms;  // simulate a ~60fps poll
      if (!gate.should_attempt(t)) continue;
      ++attempted;
      if (gate.on_failure(t)) ++logged;
    }
    assert(logged == 1);
    assert(attempted > 1);        // backoff still let a few real retries through over 16s of polling
    assert(attempted < 1000);     // but nowhere near once per frame
  }

  return 0;
}
