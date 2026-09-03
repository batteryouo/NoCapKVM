#pragma once
#include <cstdint>
#include <vector>

namespace nockvm::discovery {

// Continuing the shared msg_type numbering after kMsgGoAway (= 11).
// Bidirectional -- either TcpServer or TcpClient may send or receive
// either one, whenever that side's own local clipboard changes.
constexpr uint8_t kMsgClipboardText = 12;   // payload: raw UTF-8 bytes
constexpr uint8_t kMsgClipboardImage = 13;  // payload: raw JPEG bytes

enum class ClipboardMsgType : uint8_t { Text, Jpeg };

// No dedicated encode/decode pair -- both payloads are already exactly the
// bytes to send (UTF-8 text, JPEG-encoded image), so the msg_type alone is
// enough framing; this struct just carries a received one back out of
// TcpServer/TcpClient's take_pending_clipboard().
struct ClipboardMessage {
  ClipboardMsgType type;
  std::vector<uint8_t> data;
};

}  // namespace nockvm::discovery
