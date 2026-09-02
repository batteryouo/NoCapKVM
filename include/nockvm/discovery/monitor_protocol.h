#pragma once
#include <cstdint>
#include <vector>
#include "nockvm/display/monitor_info.h"

namespace nockvm::discovery {

constexpr uint8_t kMsgMonitorList = 1;

std::vector<uint8_t> encode_monitor_list(const std::vector<display::MonitorInfo>& monitors);
bool decode_monitor_list(const uint8_t* data, size_t len, std::vector<display::MonitorInfo>& out);

}  // namespace nockvm::discovery
