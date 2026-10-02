#include "nockvm/telemetry/counters.h"

namespace nockvm::telemetry {

Counters& counters() {
  static Counters instance;
  return instance;
}

void record_hook_lag(uint32_t lag_ms) {
  Counters& c = counters();
  c.hook_events.fetch_add(1, std::memory_order_relaxed);
  if (lag_ms >= kHookLagSlowMs) c.hook_lag_slow.fetch_add(1, std::memory_order_relaxed);
  uint32_t prev = c.hook_lag_max_ms.load(std::memory_order_relaxed);
  while (lag_ms > prev && !c.hook_lag_max_ms.compare_exchange_weak(prev, lag_ms, std::memory_order_relaxed)) {
  }
}

}  // namespace nockvm::telemetry
