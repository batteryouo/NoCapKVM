#include "nockvm/telemetry/telemetry.h"
#include <chrono>
#include <memory>
#include "nockvm/telemetry/counter_delta.h"
#include "nockvm/telemetry/counters.h"
#include "nockvm/telemetry/format.h"
#include "nockvm/telemetry/logger.h"
#include "nockvm/telemetry/process_stats.h"

namespace nockvm::telemetry {
namespace {

// Flags a report interval as having "sustained" underruns rather than
// isolated ones -- ordinary single-packet loss self-heals within the same
// interval and isn't worth a line; this many in one interval means
// playback is falling behind for real.
constexpr uint64_t kSustainedUnderrunThreshold = 100;

std::unique_ptr<RotatingLogger> g_logger;
CpuSampler g_cpu_sampler;
std::chrono::steady_clock::time_point g_start_time;
std::chrono::steady_clock::time_point g_interval_start;

uint64_t g_frame_count_interval = 0;
double g_frame_time_sum_ms = 0.0;
double g_frame_time_max_ms = 0.0;

uint64_t g_prev_audio_received = 0;
uint64_t g_prev_audio_underruns = 0;
uint64_t g_prev_raw_input_events = 0;
GenerationCounter g_prev_jitter_cap;

void write(const std::string& line) {
  if (!g_logger) return;
  const std::string stamped = format_timestamp(std::chrono::system_clock::now()) + " " + line;
  const int before = g_logger->rotation_count();
  g_logger->write_line(stamped);
  if (g_logger->rotation_count() != before) {
    g_logger->write_line(format_timestamp(std::chrono::system_clock::now()) + " " +
                          format_event_line("telemetry.rotated", "log file rotated"));
  }
}

}  // namespace

void init(const std::filesystem::path& dir) {
  auto logger = std::make_unique<RotatingLogger>(dir, "telemetry.log", kMaxLogFileBytes, kMaxLogFiles);
  if (!logger->open()) return;
  g_logger = std::move(logger);
  g_start_time = std::chrono::steady_clock::now();
  g_interval_start = g_start_time;
  write(format_event_line("telemetry.started", "telemetry logging initialized"));
}

void log_event(std::string_view key, const std::string& message) { write(format_event_line(key, message)); }

void tick(double frame_time_ms, const FrameContext& ctx) {
  if (!g_logger) return;

  ++g_frame_count_interval;
  g_frame_time_sum_ms += frame_time_ms;
  if (frame_time_ms > g_frame_time_max_ms) g_frame_time_max_ms = frame_time_ms;

  const auto now = std::chrono::steady_clock::now();
  const double interval_s = std::chrono::duration<double>(now - g_interval_start).count();
  if (interval_s < kReportIntervalSeconds) return;

  Counters& c = counters();
  const uint64_t audio_received = c.audio_packets_received.load(std::memory_order_relaxed);
  const uint64_t audio_decoded = c.audio_packets_decoded.load(std::memory_order_relaxed);
  const uint64_t audio_rejected = c.audio_packets_rejected.load(std::memory_order_relaxed);
  const uint64_t audio_dropped = c.audio_packets_dropped_cap.load(std::memory_order_relaxed);
  const uint64_t audio_underruns = c.audio_underruns.load(std::memory_order_relaxed);
  const uint64_t raw_input_events = c.raw_input_events.load(std::memory_order_relaxed);
  const uint64_t raw_input_processed = c.raw_input_processed.load(std::memory_order_relaxed);
  const uint64_t clipboard_sent = c.clipboard_sent.load(std::memory_order_relaxed);
  const uint64_t clipboard_received = c.clipboard_received.load(std::memory_order_relaxed);
  const uint64_t audio_slave_start_failures = c.audio_slave_start_failures.load(std::memory_order_relaxed);

  const uint64_t underrun_delta = audio_underruns - g_prev_audio_underruns;
  const GenerationCounter current_jitter_cap{ctx.audio_playback_generation, ctx.audio_jitter_cap_episodes};
  const uint64_t cap_episode_delta = generation_delta(g_prev_jitter_cap, current_jitter_cap);

  PeriodicSummary s;
  s.uptime_s = static_cast<uint64_t>(std::chrono::duration<double>(now - g_start_time).count());
  s.cpu_pct = g_cpu_sampler.sample();
  s.working_set_mib = working_set_mib();
  s.private_mib = private_mib();
  s.handles = handle_count();
  s.fps = interval_s > 0 ? static_cast<double>(g_frame_count_interval) / interval_s : 0.0;
  s.frame_avg_ms =
      g_frame_count_interval > 0 ? g_frame_time_sum_ms / static_cast<double>(g_frame_count_interval) : 0.0;
  s.frame_max_ms = g_frame_time_max_ms;
  s.window_visible = ctx.window_visible;
  s.audio_buffer_depth = ctx.audio_buffer_depth;
  s.audio_buffer_cap = ctx.audio_buffer_cap;
  s.audio_rx_pps = interval_s > 0 ? static_cast<double>(audio_received - g_prev_audio_received) / interval_s : 0.0;
  s.audio_packets_received = audio_received;
  s.audio_packets_decoded = audio_decoded;
  s.audio_packets_rejected = audio_rejected;
  s.audio_packets_dropped = audio_dropped;
  s.audio_underruns = audio_underruns;
  s.audio_slave_start_failures = audio_slave_start_failures;
  s.raw_input_pps =
      interval_s > 0 ? static_cast<double>(raw_input_events - g_prev_raw_input_events) / interval_s : 0.0;
  s.raw_input_events = raw_input_events;
  s.raw_input_processed = raw_input_processed;
  s.clipboard_sent = clipboard_sent;
  s.clipboard_received = clipboard_received;
  s.connected = ctx.connected;
  s.connection_state = ctx.connection_state;
  s.role = ctx.role;

  write(format_periodic_summary(s));

  if (underrun_delta >= kSustainedUnderrunThreshold) {
    write(format_event_line("audio.sustained_underruns", "underruns=" + std::to_string(underrun_delta) +
                                                               " over_last_s=" +
                                                               std::to_string(static_cast<int>(interval_s))));
  }

  if (cap_episode_delta > 0) {
    write(format_event_line("audio.jitter_buffer_cap_reached",
                             "episodes=" + std::to_string(cap_episode_delta) +
                                 " max_depth=" + std::to_string(ctx.audio_buffer_cap) +
                                 " total_dropped=" + std::to_string(audio_dropped)));
  }

  g_prev_audio_received = audio_received;
  g_prev_audio_underruns = audio_underruns;
  g_prev_raw_input_events = raw_input_events;
  g_prev_jitter_cap = current_jitter_cap;
  g_frame_count_interval = 0;
  g_frame_time_sum_ms = 0.0;
  g_frame_time_max_ms = 0.0;
  g_interval_start = now;
}

}  // namespace nockvm::telemetry
