#pragma once
#include <cstdint>
#include <vector>
#include "nockvm/display/monitor_info.h"

namespace nockvm::discovery {

constexpr uint8_t kMsgMonitorList = 1;

// Continuing the shared msg_type numbering after audio's kMsgAudioControl
// (= 9). Sent periodically by both TcpServer and TcpClient while Connected,
// independent of any real traffic, so each side can tell the other is still
// alive even during an idle stretch with nothing else to send (e.g. Master
// owns input and the mouse hasn't crossed to Slave) -- without this, a
// silently severed link (cable pulled, no FIN/RST) would never surface as
// anything but an indefinite string of receive timeouts on either side.
// Carries no payload; receiving it (like receiving anything else) is enough
// to reset the sender's own "still alive" deadline.
constexpr uint8_t kMsgHeartbeat = 10;

std::vector<uint8_t> encode_monitor_list(const std::vector<display::MonitorInfo>& monitors);
bool decode_monitor_list(const uint8_t* data, size_t len, std::vector<display::MonitorInfo>& out);

}  // namespace nockvm::discovery
