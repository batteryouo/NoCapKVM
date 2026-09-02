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

// Sent by Master right before it closes an active connection on purpose --
// either the user hit Disconnect, or a previously-kicked (Master-initiated
// disconnect) device tried to reconnect and was rejected -- as opposed to
// the connection just dying underneath it (silence timeout, a real socket
// error). No payload; receiving it tells Slave to stop auto-connecting to
// this specific Master for the rest of the current session, until the next
// time a connection to it actually succeeds. Slave disconnecting on its own
// initiative sends nothing -- Master doesn't need to know, and doesn't
// change its own trust of that Slave either way.
constexpr uint8_t kMsgGoAway = 11;

std::vector<uint8_t> encode_monitor_list(const std::vector<display::MonitorInfo>& monitors);
bool decode_monitor_list(const uint8_t* data, size_t len, std::vector<display::MonitorInfo>& out);

}  // namespace nockvm::discovery
