#pragma once
#include <chrono>
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <sys/socket.h>
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
socket_t create_tcp_socket();
void close_socket(socket_t sock);
bool set_broadcast(socket_t sock);
bool set_reuse_address(socket_t sock);
bool set_receive_timeout(socket_t sock, std::chrono::milliseconds timeout);

// True if the socket becomes readable (or, for a listening socket, has a
// pending connection) before the timeout elapses.
bool wait_readable(socket_t sock, std::chrono::milliseconds timeout);

// A plain blocking connect() to an unreachable host can hang for the OS's
// own SYN-retry timeout, which can run into tens of seconds to minutes --
// and since TcpClient::stop() joins its background thread synchronously,
// a hang inside connect() there would freeze whoever destroys/replaces the
// TcpClient (e.g. the UI thread on Back or a second Connect click) for
// just as long. This bounds that wait explicitly instead. Temporarily
// makes sock non-blocking and always restores it to blocking mode before
// returning, regardless of outcome, since callers assume blocking
// semantics for every socket call afterward (send_all/recv_all, the
// SO_RCVTIMEO set via set_receive_timeout).
bool connect_with_timeout(socket_t sock, const sockaddr* addr, size_t addr_len, std::chrono::milliseconds timeout);

// Loops send()/recv() since TCP can transfer fewer bytes than requested
// per call, unlike this codebase's UDP sendto()/recvfrom() calls.
bool send_all(socket_t sock, const uint8_t* data, size_t len);
bool recv_all(socket_t sock, uint8_t* buf, size_t len, std::chrono::steady_clock::time_point deadline);

}  // namespace nockvm::discovery
