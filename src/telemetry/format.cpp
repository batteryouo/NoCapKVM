#include "nockvm/telemetry/format.h"
#include <cstdio>
#include <ctime>

namespace nockvm::telemetry {

std::string format_timestamp(std::chrono::system_clock::time_point tp) {
  const std::time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

std::string format_periodic_summary(const PeriodicSummary& s) {
  char buf[768];
  std::snprintf(buf, sizeof(buf),
                "uptime_s=%llu cpu_pct=%.1f working_set_mib=%.0f private_mib=%.0f handles=%u "
                "fps=%.1f frame_avg_ms=%.1f frame_max_ms=%.1f window_visible=%s "
                "audio_buffer=%zu/%zu audio_rx_pps=%.1f audio_rx=%llu audio_decoded=%llu "
                "audio_rejected=%llu audio_dropped=%llu audio_underruns=%llu audio_start_failures=%llu "
                "raw_input_pps=%.1f raw_input_total=%llu raw_input_processed=%llu "
                "clipboard_sent=%llu clipboard_received=%llu connected=%s role=%s state=%s",
                static_cast<unsigned long long>(s.uptime_s), s.cpu_pct, s.working_set_mib, s.private_mib, s.handles,
                s.fps, s.frame_avg_ms, s.frame_max_ms, s.window_visible ? "true" : "false", s.audio_buffer_depth,
                s.audio_buffer_cap, s.audio_rx_pps, static_cast<unsigned long long>(s.audio_packets_received),
                static_cast<unsigned long long>(s.audio_packets_decoded),
                static_cast<unsigned long long>(s.audio_packets_rejected),
                static_cast<unsigned long long>(s.audio_packets_dropped),
                static_cast<unsigned long long>(s.audio_underruns),
                static_cast<unsigned long long>(s.audio_slave_start_failures), s.raw_input_pps,
                static_cast<unsigned long long>(s.raw_input_events),
                static_cast<unsigned long long>(s.raw_input_processed),
                static_cast<unsigned long long>(s.clipboard_sent),
                static_cast<unsigned long long>(s.clipboard_received), s.connected ? "true" : "false",
                s.role.c_str(), s.connection_state.c_str());
  return buf;
}

std::string format_event_line(std::string_view key, std::string_view message) {
  std::string escaped;
  escaped.reserve(message.size());
  for (char c : message) {
    if (c == '"' || c == '\\') escaped += '\\';
    escaped += c;
  }

  std::string out;
  out.reserve(key.size() + escaped.size() + 16);
  out += "event=";
  out += key;
  out += " message=\"";
  out += escaped;
  out += '"';
  return out;
}

}  // namespace nockvm::telemetry
