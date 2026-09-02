#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nockvm::display {

struct MonitorInfo {
  int32_t x = 0, y = 0;  // position in virtual-desktop / X-screen space — this *is* the arrangement
  int32_t width = 0, height = 0;
  bool primary = false;
  std::string name;  // e.g. "\\.\DISPLAY1" (Win32) or the RandR output name (Linux)

  bool operator==(const MonitorInfo& other) const {
    return x == other.x && y == other.y && width == other.width && height == other.height &&
           primary == other.primary && name == other.name;
  }
  bool operator!=(const MonitorInfo& other) const { return !(*this == other); }
};

std::vector<MonitorInfo> get_local_monitors();

}  // namespace nockvm::display
