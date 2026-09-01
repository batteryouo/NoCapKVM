#pragma once
#include <cstdint>
#include <string>

namespace nockvm::discovery {

enum class ConnectionState : uint8_t { Idle, Connecting, Connected, Failed };

struct ConnectionInfo {
  ConnectionState state = ConnectionState::Idle;
  uint64_t peer_device_id = 0;
  std::string peer_ip;
};

}  // namespace nockvm::discovery
