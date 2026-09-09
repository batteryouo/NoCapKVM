#include "nockvm/telemetry/process_stats.h"
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#endif

namespace nockvm::telemetry {

#ifdef _WIN32

double working_set_mib() {
  PROCESS_MEMORY_COUNTERS pmc{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0.0;
  return static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
}

double private_mib() {
  PROCESS_MEMORY_COUNTERS_EX pmc{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
    return 0.0;
  }
  return static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
}

uint32_t handle_count() {
  DWORD count = 0;
  if (!GetProcessHandleCount(GetCurrentProcess(), &count)) return 0;
  return static_cast<uint32_t>(count);
}

namespace {

uint64_t filetime_to_u64(const FILETIME& ft) {
  return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

struct CpuState {
  uint64_t last_kernel_100ns = 0;
  uint64_t last_user_100ns = 0;
  std::chrono::steady_clock::time_point last_wall{};
  bool has_baseline = false;
};

}  // namespace

CpuSampler::CpuSampler() { state_ = new CpuState(); }
CpuSampler::~CpuSampler() { delete static_cast<CpuState*>(state_); }

double CpuSampler::sample() {
  auto* s = static_cast<CpuState*>(state_);
  FILETIME creation, exit, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return 0.0;

  const uint64_t kernel_100ns = filetime_to_u64(kernel);
  const uint64_t user_100ns = filetime_to_u64(user);
  const auto now = std::chrono::steady_clock::now();

  if (!s->has_baseline) {
    s->last_kernel_100ns = kernel_100ns;
    s->last_user_100ns = user_100ns;
    s->last_wall = now;
    s->has_baseline = true;
    return 0.0;
  }

  const double cpu_delta_s =
      static_cast<double>((kernel_100ns - s->last_kernel_100ns) + (user_100ns - s->last_user_100ns)) / 1e7;
  const double wall_delta_s = std::chrono::duration<double>(now - s->last_wall).count();

  s->last_kernel_100ns = kernel_100ns;
  s->last_user_100ns = user_100ns;
  s->last_wall = now;

  if (wall_delta_s <= 0.0) return 0.0;
  return (cpu_delta_s / wall_delta_s) * 100.0;
}

#else  // !_WIN32

double working_set_mib() {
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("VmRSS:", 0) == 0) {
      long kb = 0;
      std::sscanf(line.c_str() + 6, "%ld", &kb);
      return static_cast<double>(kb) / 1024.0;
    }
  }
  return 0.0;
}

double private_mib() {
  std::ifstream f("/proc/self/smaps_rollup");
  std::string line;
  long private_kb = 0;
  bool found = false;
  while (std::getline(f, line)) {
    long kb = 0;
    if (line.rfind("Private_Clean:", 0) == 0) {
      std::sscanf(line.c_str() + 14, "%ld", &kb);
      private_kb += kb;
      found = true;
    } else if (line.rfind("Private_Dirty:", 0) == 0) {
      std::sscanf(line.c_str() + 14, "%ld", &kb);
      private_kb += kb;
      found = true;
    }
  }
  if (!found) return working_set_mib();  // smaps_rollup unavailable on this kernel -- fall back to resident
  return static_cast<double>(private_kb) / 1024.0;
}

uint32_t handle_count() {
  std::error_code ec;
  uint32_t count = 0;
  std::filesystem::directory_iterator it("/proc/self/fd", ec);
  if (ec) return 0;
  for (const auto& entry : it) {
    (void)entry;
    ++count;
  }
  return count;
}

namespace {

struct CpuState {
  uint64_t last_ticks = 0;
  std::chrono::steady_clock::time_point last_wall{};
  bool has_baseline = false;
  long clk_tck = 100;
};

// utime/stime are fields 14/15 of /proc/self/stat (1-indexed), but field 2
// (the process name) is itself free-form text that may contain spaces or
// parentheses -- skip past the last ')' before counting fields, same
// technique procps itself uses.
uint64_t read_proc_self_stat_ticks() {
  std::ifstream f("/proc/self/stat");
  std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const auto paren = content.rfind(')');
  if (paren == std::string::npos) return 0;

  std::istringstream rest(content.substr(paren + 1));
  std::vector<std::string> fields;
  std::string field;
  while (rest >> field) fields.push_back(field);
  // fields[0] is overall field 3 (state); utime is overall field 14 (index
  // 11 here), stime is overall field 15 (index 12).
  if (fields.size() < 13) return 0;
  return std::strtoull(fields[11].c_str(), nullptr, 10) + std::strtoull(fields[12].c_str(), nullptr, 10);
}

}  // namespace

CpuSampler::CpuSampler() {
  auto* s = new CpuState();
  s->clk_tck = sysconf(_SC_CLK_TCK);
  if (s->clk_tck <= 0) s->clk_tck = 100;
  state_ = s;
}

CpuSampler::~CpuSampler() { delete static_cast<CpuState*>(state_); }

double CpuSampler::sample() {
  auto* s = static_cast<CpuState*>(state_);
  const uint64_t ticks = read_proc_self_stat_ticks();
  const auto now = std::chrono::steady_clock::now();

  if (!s->has_baseline) {
    s->last_ticks = ticks;
    s->last_wall = now;
    s->has_baseline = true;
    return 0.0;
  }

  const double cpu_delta_s = static_cast<double>(ticks - s->last_ticks) / static_cast<double>(s->clk_tck);
  const double wall_delta_s = std::chrono::duration<double>(now - s->last_wall).count();

  s->last_ticks = ticks;
  s->last_wall = now;

  if (wall_delta_s <= 0.0) return 0.0;
  return (cpu_delta_s / wall_delta_s) * 100.0;
}

#endif

}  // namespace nockvm::telemetry
