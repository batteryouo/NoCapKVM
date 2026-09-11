#include <cassert>
#include <chrono>
#include "audio_loss_window.h"

using namespace nockvm::app;
using namespace std::chrono_literals;

int main() {
  // Startup: not ready until a full window's worth of time has actually
  // been observed since the first sample, regardless of how many samples
  // arrived in that time.
  {
    AudioLossWindow w(60s);
    const auto t0 = std::chrono::steady_clock::time_point(1000s);
    assert(!w.reading().ready);

    w.record(t0, 0, 0);
    assert(!w.reading().ready);  // first sample alone isn't a full window

    for (int i = 1; i <= 59; ++i) {
      w.record(t0 + std::chrono::seconds(i), static_cast<uint64_t>(i), static_cast<uint64_t>(i * 10));
      assert(!w.reading().ready);  // still short of 60s since t0
    }
  }

  // Normal deltas: once the window is full, misses/attempts are the delta
  // between the oldest retained sample and the most recent one -- not the
  // lifetime totals.
  {
    AudioLossWindow w(60s);
    const auto t0 = std::chrono::steady_clock::time_point(2000s);
    for (int i = 0; i <= 60; ++i) {
      // misses grows by 1/s, played grows by 9/s -- a steady 10% loss rate.
      w.record(t0 + std::chrono::seconds(i), static_cast<uint64_t>(i), static_cast<uint64_t>(i * 9));
    }
    const auto r = w.reading();
    assert(r.ready);
    // Oldest retained sample is t0 itself (see AudioLossWindow::record's
    // eviction comment -- the sample exactly at the window edge is kept).
    assert(r.misses == 60);
    assert(r.attempts == 60 + 60 * 9);

    // Advancing further slides the window: old samples fall out, the delta
    // keeps reflecting only the trailing ~60s, not the lifetime total.
    for (int i = 61; i <= 120; ++i) {
      w.record(t0 + std::chrono::seconds(i), static_cast<uint64_t>(i), static_cast<uint64_t>(i * 9));
    }
    const auto r2 = w.reading();
    assert(r2.ready);
    assert(r2.misses <= 60);  // never exceeds one window's worth of growth
    assert(r2.misses > 0);
  }

  // Reset: clears history and readiness, so a fresh instance's counters
  // (e.g. after AudioPlayback is reconstructed) are never compared against
  // a stale baseline from before the reset.
  {
    AudioLossWindow w(60s);
    const auto t0 = std::chrono::steady_clock::time_point(3000s);
    for (int i = 0; i <= 60; ++i) w.record(t0 + std::chrono::seconds(i), 100, 900);
    assert(w.reading().ready);

    w.reset();
    assert(!w.reading().ready);
    const auto r = w.reading();
    assert(r.misses == 0 && r.attempts == 0);

    // A sample recorded right after reset starts a fresh window rather
    // than reusing the old first_sample_at_.
    w.record(t0 + 61s, 5, 5);
    assert(!w.reading().ready);
    w.record(t0 + 61s + 60s, 15, 25);
    assert(w.reading().ready);
    assert(w.reading().misses == 10);
    assert(w.reading().attempts == 30);
  }

  // Counter changes: a burst of misses partway through the window still
  // shows up correctly once the window has filled around it, and a
  // denominator of misses+played (not some other total) is used.
  {
    AudioLossWindow w(60s);
    const auto t0 = std::chrono::steady_clock::time_point(4000s);
    w.record(t0, 0, 0);
    w.record(t0 + 30s, 0, 300);    // no misses for the first half
    w.record(t0 + 60s, 50, 550);   // a burst of 50 misses in the second half
    const auto r = w.reading();
    assert(r.ready);
    assert(r.misses == 50);
    assert(r.attempts == 50 + 550);
  }

  // A gap between samples longer than the window itself (e.g. the app was
  // suspended, or the main loop stalled) must not be reported as a "last
  // 60 s" rate -- that would silently span the whole gap under a label
  // that claims only the last 60 seconds. The stale history is discarded
  // instead, and the window has to fill up again from scratch.
  {
    AudioLossWindow w(60s);
    const auto t0 = std::chrono::steady_clock::time_point(5000s);
    w.record(t0, 10, 90);
    w.record(t0 + 90s, 12, 108);  // gap of 90s > the 60s window
    assert(!w.reading().ready);   // stale history discarded, not misreported as "last 60s"
    const auto r = w.reading();
    assert(r.misses == 0 && r.attempts == 0);

    // A fresh window accumulated after the gap becomes ready normally,
    // measured only from the post-gap sample onward.
    for (int i = 1; i <= 60; ++i) {
      w.record(t0 + 90s + std::chrono::seconds(i), static_cast<uint64_t>(12 + i), static_cast<uint64_t>(108 + i * 4));
    }
    const auto r2 = w.reading();
    assert(r2.ready);
    assert(r2.misses == 60);
    assert(r2.attempts == 60 + 60 * 4);
  }

  // A gap exactly equal to the window is still accepted as a normal (if
  // sparse) sample rather than triggering a reset -- only a gap strictly
  // longer than the window discards history.
  {
    AudioLossWindow w(60s);
    const auto t0 = std::chrono::steady_clock::time_point(6000s);
    w.record(t0, 10, 90);
    w.record(t0 + 60s, 12, 108);  // gap of exactly 60s -- not > window
    const auto r = w.reading();
    assert(r.ready);
    assert(r.misses == 2);
    assert(r.attempts == 2 + 18);
  }

  return 0;
}
