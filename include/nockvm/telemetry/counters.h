#pragma once
#include <atomic>
#include <cstdint>

namespace nockvm::telemetry {

// Process-wide counters fed from hot paths -- raw input dispatch, audio
// network receive, the jitter buffer, playback callbacks, clipboard sync.
// Every member is atomic so any thread can increment it without a lock;
// the periodic reporter (telemetry.cpp) is the only reader, and it only
// ever reads from the main thread. Updating one of these must stay a
// lock-free atomic operation -- no formatting, no I/O, no allocation.
struct Counters {
  std::atomic<uint64_t> raw_input_events{0};
  // Count of raw-input events that actually ran the processing path (only
  // needed while input is suppressed) -- see input_service.cpp.
  std::atomic<uint64_t> raw_input_processed{0};

  std::atomic<uint64_t> audio_packets_received{0};
  std::atomic<uint64_t> audio_packets_decoded{0};
  std::atomic<uint64_t> audio_packets_rejected{0};
  std::atomic<uint64_t> audio_packets_dropped_cap{0};
  std::atomic<uint64_t> audio_underruns{0};
  // Count of Slave AudioCapture::start() failures -- see
  // audio_pump.cpp's pump_slave().
  std::atomic<uint64_t> audio_slave_start_failures{0};

  std::atomic<uint64_t> clipboard_sent{0};
  std::atomic<uint64_t> clipboard_received{0};

  // Low-level hook delivery lag, fed by record_hook_lag(). hook_lag_max_ms
  // is the maximum since the last periodic report; the reporter resets it.
  std::atomic<uint64_t> hook_events{0};
  std::atomic<uint64_t> hook_lag_slow{0};
  std::atomic<uint32_t> hook_lag_max_ms{0};
};

Counters& counters();

// A hook event delivered at least this late counts toward hook_lag_slow.
constexpr uint32_t kHookLagSlowMs = 50;

// Records how long one WH_MOUSE_LL/WH_KEYBOARD_LL event waited between
// being timestamped by the OS and reaching the hook callback. Safe to call
// from the hook callback itself.
void record_hook_lag(uint32_t lag_ms);

}  // namespace nockvm::telemetry
