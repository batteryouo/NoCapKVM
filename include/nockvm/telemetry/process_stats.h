#pragma once
#include <cstdint>

namespace nockvm::telemetry {

// This process's resident ("working set") memory, in MiB.
double working_set_mib();

// This process's private (non-shared) memory, in MiB -- on Windows,
// PROCESS_MEMORY_COUNTERS_EX::PrivateUsage; on Linux, /proc/self/
// smaps_rollup's Private_Clean+Private_Dirty, falling back to working-set
// if smaps_rollup isn't available.
double private_mib();

// Open handle count on Windows; open file-descriptor count on Linux (not
// the same concept, but the closest cheap analog -- both flag "this
// process is leaking OS-level resources over time").
uint32_t handle_count();

// Stateful: sample() reports this process's CPU usage, as a percentage of
// one logical core (so N fully-busy cores read N*100), since the previous
// sample() call. The first call after construction has nothing to compare
// against and returns 0.
class CpuSampler {
public:
  CpuSampler();
  ~CpuSampler();
  CpuSampler(const CpuSampler&) = delete;
  CpuSampler& operator=(const CpuSampler&) = delete;

  double sample();

private:
  void* state_ = nullptr;
};

}  // namespace nockvm::telemetry
