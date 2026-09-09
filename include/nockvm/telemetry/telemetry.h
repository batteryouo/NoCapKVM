#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace nockvm::telemetry {

constexpr size_t kMaxLogFileBytes = 5u * 1024 * 1024;
constexpr int kMaxLogFiles = 3;
constexpr int kReportIntervalSeconds = 60;

// One-time setup: opens the bounded rotating log file under dir (a
// subdirectory of the app's own config directory -- never the repo). Call
// once from main() before the first tick()/log_event(). If this fails
// (e.g. the directory can't be created), every other function here
// quietly becomes a no-op rather than crashing the app.
void init(const std::filesystem::path& dir);

// Snapshot gathered once per frame on the main thread -- cheap to compute,
// no file I/O -- that feeds whichever periodic summary next comes due.
struct FrameContext {
  bool window_visible = true;
  size_t audio_buffer_depth = 0;
  size_t audio_buffer_cap = 0;
  // Cumulative jitter-buffer "at cap" episode count -- see
  // JitterBuffer::cap_episodes() -- and an identifier for the AudioPlayback
  // instance it came from, since the count restarts at 0 whenever playback
  // is recreated. See counter_delta.h's GenerationCounter.
  uint64_t audio_jitter_cap_episodes = 0;
  uint64_t audio_playback_generation = 0;
  bool connected = false;
  std::string role;              // "master" or "slave"
  std::string connection_state;  // e.g. "connected", "idle", "pairing"
};

// Call once per frame from the main loop, right after measuring this
// frame's wall-clock duration. Accumulates frame stats; once
// kReportIntervalSeconds have elapsed since the last summary, formats and
// writes one periodic line (see format.h), then resets the interval
// accumulators. The only file I/O here happens on that once-per-interval
// write, always on the main thread.
void tick(double frame_time_ms, const FrameContext& ctx);

// Logs one immediate state-transition line -- connection/disconnection,
// audio start/stop/restart, window hide/show, jitter buffer cap reached,
// sustained audio underruns, or a log-rotation notice. Main-thread only:
// never call this from an audio callback, input hook, or network receive
// hot path -- it performs file I/O.
void log_event(std::string_view key, const std::string& message);

}  // namespace nockvm::telemetry
