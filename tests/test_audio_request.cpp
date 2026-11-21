#include <cassert>
#include <chrono>
#include "audio_request.h"

using namespace nockvm::app;
using namespace std::chrono_literals;

namespace {

const auto t0 = std::chrono::steady_clock::time_point(100s);
const AudioSettings kOn48{true, {48000, 16}};
const AudioSettings kOff48{false, {48000, 16}};
const AudioSettings kOn24{true, {24000, 8}};

}  // namespace

int main() {
  // With no request, the UI shows exactly what Slave reports, including a
  // change made on Slave's side.
  {
    AudioRequest r(2s);
    assert(r.shown(kOn48, 0, t0) == kOn48);
    assert(r.shown(kOff48, 0, t0 + 10ms) == kOff48);
    assert(!r.pending());
    assert(!r.take_unsent());
  }

  // A click is sent exactly once with a nonzero id, and its value is shown
  // while Slave's report does not acknowledge it yet.
  {
    AudioRequest r(2s);
    r.request(kOff48, t0);
    const auto sent = r.take_unsent();
    assert(sent && sent->settings == kOff48 && sent->id != 0);
    assert(!r.take_unsent());
    assert(r.shown(kOn48, 0, t0 + 50ms) == kOff48);
    assert(r.pending());
  }

  // A report acknowledging the request's id ends it; later Slave-side
  // changes show again.
  {
    AudioRequest r(2s);
    r.request(kOff48, t0);
    const auto sent = r.take_unsent();
    assert(r.shown(kOff48, sent->id, t0 + 20ms) == kOff48);
    assert(!r.pending());
    assert(r.shown(kOn24, sent->id, t0 + 30ms) == kOn24);
  }

  // A report whose value happens to match but which does not acknowledge
  // the request (Slave's report from before the click) keeps it pending.
  {
    AudioRequest r(2s);
    r.request(kOn24, t0);
    const auto sent = r.take_unsent();
    assert(r.shown(kOn24, sent->id - 1, t0 + 10ms) == kOn24);
    assert(r.pending());
  }

  // Quick "off, then on" from "on": the stale "on" report and the report
  // acknowledging only the first click must not end the second click, so
  // the UI never flicks back to "off".
  {
    AudioRequest r(2s);
    r.request(kOff48, t0);
    const auto first = r.take_unsent();
    r.request(kOn48, t0 + 30ms);
    const auto second = r.take_unsent();
    assert(first && second && second->id != first->id);
    assert(r.shown(kOn48, 0, t0 + 40ms) == kOn48);  // stale report from before both clicks
    assert(r.pending());
    assert(r.shown(kOff48, first->id, t0 + 60ms) == kOn48);  // Slave applied only the first so far
    assert(r.pending());
    assert(r.shown(kOn48, second->id, t0 + 80ms) == kOn48);
    assert(!r.pending());
  }

  // Slave applying only the latest of several queued requests acknowledges
  // that later id, which also covers the earlier one.
  {
    AudioRequest r(2s);
    r.request(kOff48, t0);
    r.take_unsent();
    r.request(kOn24, t0 + 10ms);
    const auto latest = r.take_unsent();
    assert(r.shown(kOn24, latest->id, t0 + 20ms) == kOn24);
    assert(!r.pending());
  }

  // If Slave never acknowledges (an older Slave reports id 0), the UI
  // falls back to Slave's report at the timeout instead of showing the
  // request forever.
  {
    AudioRequest r(2s);
    r.request(kOff48, t0);
    assert(r.shown(kOn48, 0, t0 + 1999ms) == kOff48);
    assert(r.shown(kOn48, 0, t0 + 2s) == kOn48);
    assert(!r.pending());
  }

  // A second click restarts the timeout.
  {
    AudioRequest r(2s);
    r.request(kOff48, t0);
    r.request(kOn24, t0 + 1500ms);
    assert(r.shown(kOn48, 0, t0 + 3s) == kOn24);
    assert(r.shown(kOn48, 0, t0 + 3500ms) == kOn48);
  }

  // reset() (disconnect) drops both the pending value and an unsent
  // request, and ids keep increasing so an id from the old connection is
  // never reused.
  {
    AudioRequest r(2s);
    r.request(kOff48, t0);
    const auto before = r.take_unsent();
    r.request(kOn24, t0);
    r.reset();
    assert(!r.pending());
    assert(!r.take_unsent());
    assert(r.shown(kOn48, 0, t0) == kOn48);
    r.request(kOff48, t0 + 1s);
    const auto after = r.take_unsent();
    assert(after->id > before->id + 1);
  }

  return 0;
}
