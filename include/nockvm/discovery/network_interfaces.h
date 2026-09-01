#pragma once
#include <cstdint>
#include <vector>

namespace nockvm::discovery {

// Returns the subnet-directed broadcast address (network byte order,
// e.g. suitable for sockaddr_in::sin_addr.s_addr) of every active,
// non-loopback IPv4 interface on this machine. Sending to these
// addresses individually is more reliable than the limited broadcast
// address (255.255.255.255) on multi-homed machines, where the OS's
// routing table can pick an unrelated interface for that address.
std::vector<uint32_t> get_broadcast_targets();

}  // namespace nockvm::discovery
