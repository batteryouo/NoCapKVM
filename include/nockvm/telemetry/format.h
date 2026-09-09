#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nockvm::telemetry {

struct PeriodicSummary {
  uint64_t uptime_s = 0;
  double cpu_pct = 0.0;
  double working_set_mib = 0.0;
  double private_mib = 0.0;
  uint32_t handles = 0;
  double fps = 0.0;
  double frame_avg_ms = 0.0;
  double frame_max_ms = 0.0;
  bool window_visible = true;
  size_t audio_buffer_depth = 0;
  size_t audio_buffer_cap = 0;
  double audio_rx_pps = 0.0;
  uint64_t audio_packets_received = 0;
  uint64_t audio_packets_decoded = 0;
  uint64_t audio_packets_rejected = 0;
  uint64_t audio_packets_dropped = 0;
  uint64_t audio_underruns = 0;
  uint64_t audio_slave_start_failures = 0;
  double raw_input_pps = 0.0;
  uint64_t raw_input_events = 0;
  uint64_t raw_input_processed = 0;
  uint64_t clipboard_sent = 0;
  uint64_t clipboard_received = 0;
  bool connected = false;
  std::string connection_state;
  std::string role;
};

// One key=value-per-field line (space-separated, no trailing newline or
// timestamp -- callers wanting a timestamp prefix it themselves via
// format_timestamp()) so the log stays greppable/parseable without a
// custom tool.
std::string format_periodic_summary(const PeriodicSummary& s);

// key="message"-shaped event line; message is double-quote/backslash
// escaped so a message containing either can't break parsing.
std::string format_event_line(std::string_view key, std::string_view message);

// UTC "YYYY-MM-DDTHH:MM:SSZ" timestamp, shared by both line kinds above.
std::string format_timestamp(std::chrono::system_clock::time_point tp);

}  // namespace nockvm::telemetry
