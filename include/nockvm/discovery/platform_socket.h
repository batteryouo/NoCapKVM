#pragma once
#include <chrono>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

namespace nockvm::discovery {

// RAII guard around WSAStartup/WSACleanup; a no-op on non-Windows.
class SocketLibraryGuard {
public:
  SocketLibraryGuard();
  ~SocketLibraryGuard();
  SocketLibraryGuard(const SocketLibraryGuard&) = delete;
  SocketLibraryGuard& operator=(const SocketLibraryGuard&) = delete;
};

socket_t create_udp_socket();
void close_socket(socket_t sock);
bool set_broadcast(socket_t sock);
bool set_reuse_address(socket_t sock);
bool set_receive_timeout(socket_t sock, std::chrono::milliseconds timeout);

}  // namespace nockvm::discovery
