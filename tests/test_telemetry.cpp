#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include "nockvm/telemetry/counter_delta.h"
#include "nockvm/telemetry/format.h"
#include "nockvm/telemetry/logger.h"

using namespace nockvm::telemetry;

namespace {

int count_log_files(const std::filesystem::path& dir, const std::string& base_name) {
  int count = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (entry.path().filename().string().rfind(base_name, 0) == 0) ++count;
  }
  return count;
}

std::string read_file(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace

int main() {
  // RotatingLogger: never exceeds max_files total files, and the active
  // file never exceeds max_bytes once enough has been written to force at
  // least one rotation.
  {
    const auto dir = std::filesystem::temp_directory_path() / "nockvm_test_telemetry_rotation";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    // Each line is ~22 bytes ("line NNNN filler filler\n"); a 200-byte cap
    // forces a rotation roughly every ~9 lines.
    RotatingLogger logger(dir, "telemetry.log", /*max_bytes=*/200, /*max_files=*/3);
    assert(logger.open());
    for (int i = 0; i < 100; ++i) logger.write_line("line " + std::to_string(i) + " filler filler");

    assert(logger.rotation_count() > 0);
    assert(count_log_files(dir, "telemetry.log") <= 3);
    assert(std::filesystem::file_size(logger.active_path()) <= 200 + 64);  // one line's worth of slack over the cap

    std::filesystem::remove_all(dir, ec);
  }

  // RotatingLogger: with max_files=1, no backups are ever kept -- rotation
  // just truncates the single active file.
  {
    const auto dir = std::filesystem::temp_directory_path() / "nockvm_test_telemetry_single_file";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    RotatingLogger logger(dir, "telemetry.log", /*max_bytes=*/100, /*max_files=*/1);
    assert(logger.open());
    for (int i = 0; i < 50; ++i) logger.write_line("line " + std::to_string(i));

    assert(logger.rotation_count() > 0);
    assert(count_log_files(dir, "telemetry.log") == 1);

    std::filesystem::remove_all(dir, ec);
  }

  // RotatingLogger: reopening resumes appending to the existing file
  // rather than truncating it, and its recorded size accounts for what
  // was already there.
  {
    const auto dir = std::filesystem::temp_directory_path() / "nockvm_test_telemetry_resume";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    {
      RotatingLogger logger(dir, "telemetry.log", /*max_bytes=*/10000, /*max_files=*/3);
      assert(logger.open());
      logger.write_line("first line");
    }
    {
      RotatingLogger logger(dir, "telemetry.log", /*max_bytes=*/10000, /*max_files=*/3);
      assert(logger.open());
      logger.write_line("second line");
    }

    const std::string content = read_file(dir / "telemetry.log");
    assert(content.find("first line") != std::string::npos);
    assert(content.find("second line") != std::string::npos);

    std::filesystem::remove_all(dir, ec);
  }

  // RotatingLogger: a failed open() (parent path is itself a regular file,
  // not a directory) leaves write_line() a silent no-op rather than
  // crashing.
  {
    const auto dir = std::filesystem::temp_directory_path() / "nockvm_test_telemetry_bad_parent";
    std::error_code ec;
    std::filesystem::remove_all(dir.parent_path() / "nockvm_test_telemetry_bad_parent_blocker", ec);
    const auto blocker = std::filesystem::temp_directory_path() / "nockvm_test_telemetry_bad_parent_blocker";
    { std::ofstream(blocker) << "not a directory"; }
    const auto bad_dir = blocker / "subdir";  // can't be created: blocker is a file, not a directory

    RotatingLogger logger(bad_dir, "telemetry.log", 100, 3);
    assert(!logger.open());
    logger.write_line("should not throw or crash");

    std::filesystem::remove(blocker, ec);
  }

  // format_periodic_summary: every field lands in the output in the
  // documented key=value shape.
  {
    PeriodicSummary s;
    s.uptime_s = 7200;
    s.cpu_pct = 12.4;
    s.working_set_mib = 118;
    s.private_mib = 94;
    s.handles = 312;
    s.fps = 60.0;
    s.frame_avg_ms = 16.7;
    s.frame_max_ms = 48.2;
    s.window_visible = false;
    s.audio_buffer_depth = 8;
    s.audio_buffer_cap = 200;
    s.audio_rx_pps = 200.0;
    s.audio_packets_received = 12000;
    s.audio_packets_decoded = 11990;
    s.audio_packets_rejected = 3;
    s.audio_packets_dropped = 7;
    s.audio_underruns = 3;
    s.raw_input_pps = 850.0;
    s.raw_input_events = 51000;
    s.raw_input_processed = 4200;
    s.clipboard_sent = 2;
    s.clipboard_received = 1;
    s.connected = true;
    s.connection_state = "connected";
    s.role = "master";

    const std::string line = format_periodic_summary(s);
    assert(line.find("uptime_s=7200") != std::string::npos);
    assert(line.find("cpu_pct=12.4") != std::string::npos);
    assert(line.find("working_set_mib=118") != std::string::npos);
    assert(line.find("private_mib=94") != std::string::npos);
    assert(line.find("handles=312") != std::string::npos);
    assert(line.find("fps=60.0") != std::string::npos);
    assert(line.find("frame_avg_ms=16.7") != std::string::npos);
    assert(line.find("frame_max_ms=48.2") != std::string::npos);
    assert(line.find("window_visible=false") != std::string::npos);
    assert(line.find("audio_buffer=8/200") != std::string::npos);
    assert(line.find("audio_rx_pps=200.0") != std::string::npos);
    assert(line.find("audio_rx=12000") != std::string::npos);
    assert(line.find("audio_decoded=11990") != std::string::npos);
    assert(line.find("audio_rejected=3") != std::string::npos);
    assert(line.find("audio_dropped=7") != std::string::npos);
    assert(line.find("audio_underruns=3") != std::string::npos);
    assert(line.find("raw_input_pps=850.0") != std::string::npos);
    assert(line.find("raw_input_total=51000") != std::string::npos);
    assert(line.find("raw_input_processed=4200") != std::string::npos);
    assert(line.find("clipboard_sent=2") != std::string::npos);
    assert(line.find("clipboard_received=1") != std::string::npos);
    assert(line.find("connected=true") != std::string::npos);
    assert(line.find("role=master") != std::string::npos);
    assert(line.find("state=connected") != std::string::npos);
    // No embedded newline -- a caller writing this straight to a
    // line-oriented log must get exactly one line out of it.
    assert(line.find('\n') == std::string::npos);
  }

  // format_event_line: message quotes/backslashes are escaped so the line
  // stays parseable as one key="value" pair.
  {
    const std::string line = format_event_line("audio.jitter_buffer_cap_reached", "max_depth=200");
    assert(line == "event=audio.jitter_buffer_cap_reached message=\"max_depth=200\"");
  }
  {
    const std::string line = format_event_line("test.key", "has \"quotes\" and \\backslash\\");
    assert(line.find("\\\"quotes\\\"") != std::string::npos);
    assert(line.find("\\\\backslash\\\\") != std::string::npos);
  }

  // format_timestamp: fixed shape "YYYY-MM-DDTHH:MM:SSZ".
  {
    const auto tp = std::chrono::system_clock::time_point(std::chrono::seconds(1735689600));  // 2025-01-01T00:00:00Z
    const std::string ts = format_timestamp(tp);
    assert(ts.size() == 20);
    assert(ts[4] == '-' && ts[7] == '-' && ts[10] == 'T' && ts[13] == ':' && ts[16] == ':' && ts.back() == 'Z');
    assert(ts == "2025-01-01T00:00:00Z");
  }

  // generation_delta(): same generation is a plain subtraction.
  {
    const GenerationCounter prev{1, 5};
    const GenerationCounter curr{1, 8};
    assert(generation_delta(prev, curr) == 3);
  }

  // A generation change means `curr`'s value didn't grow from `prev`'s --
  // it belongs to a different instance -- so the whole value counts as new
  // rather than comparing across instances.
  {
    const GenerationCounter prev{1, 1};
    const GenerationCounter curr{2, 1};
    assert(generation_delta(prev, curr) == 1);
  }

  // Same generation but a lower value (would otherwise underflow as
  // unsigned) clamps to 0.
  {
    const GenerationCounter prev{1, 5};
    const GenerationCounter curr{1, 2};
    assert(generation_delta(prev, curr) == 0);
  }

  return 0;
}
