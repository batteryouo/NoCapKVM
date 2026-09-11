#pragma once
#include <cstdint>
#include <vector>

namespace nockvm::discovery {

// Continuing the shared msg_type numbering after kMsgClipboardImage (= 13).
// Either TcpServer or TcpClient sends kMsgPing on its own fixed interval;
// the receiving side replies with kMsgPong immediately, echoing back the
// same sequence number unchanged, so the sender can measure round-trip
// time against its own send timestamp without needing clock sync between
// the two machines.
constexpr uint8_t kMsgPing = 14;
constexpr uint8_t kMsgPong = 15;

// Shared wire shape for both message types: a single uint32 BE sequence
// number, opaque to the receiver.
std::vector<uint8_t> encode_ping_seq(uint32_t seq);
bool decode_ping_seq(const uint8_t* data, size_t len, uint32_t& seq);

}  // namespace nockvm::discovery
