#pragma once
#include <atomic>
#include <cstdint>

namespace nockvm::telemetry {

// Process-wide counters fed from hot paths -- raw input dispatch, audio
// network receive, the jitter buffer, playback callbacks, clipboard sync.
// Every member is atomic so any thread can increment it without a lock;
// the periodic reporter (telemetry.cpp) is the only reader, and it only
// ever reads from the main thread. Incrementing one of these must stay a
// single fetch_add -- no formatting, no I/O, no allocation.
struct Counters {
  std::atomic<uint64_t> raw_input_events{0};
  // Count of raw-input events that actually ran the allocating processing
  // path (only needed while input is suppressed) -- see
  // main.cpp's raw_input_wndproc.
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
};

Counters& counters();

}  // namespace nockvm::telemetry
