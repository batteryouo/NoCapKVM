#include "nockvm/discovery/platform_socket.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace nockvm::discovery {

#ifdef _WIN32
SocketLibraryGuard::SocketLibraryGuard() {
  WSADATA wsa_data;
  WSAStartup(MAKEWORD(2, 2), &wsa_data);
}
SocketLibraryGuard::~SocketLibraryGuard() { WSACleanup(); }
#else
SocketLibraryGuard::SocketLibraryGuard() {}
SocketLibraryGuard::~SocketLibraryGuard() {}
#endif

socket_t create_udp_socket() {
  static SocketLibraryGuard guard;
  return socket(AF_INET, SOCK_DGRAM, 0);
}

void close_socket(socket_t sock) {
#ifdef _WIN32
  closesocket(sock);
#else
  close(sock);
#endif
}

bool set_broadcast(socket_t sock) {
  int enable = 1;
  return setsockopt(sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&enable), sizeof(enable)) == 0;
}

bool set_reuse_address(socket_t sock) {
  int enable = 1;
  return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enable), sizeof(enable)) == 0;
}

bool set_receive_timeout(socket_t sock, std::chrono::milliseconds timeout) {
#ifdef _WIN32
  DWORD ms = static_cast<DWORD>(timeout.count());
  return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ms), sizeof(ms)) == 0;
#else
  struct timeval tv;
  tv.tv_sec = static_cast<long>(timeout.count() / 1000);
  tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
  return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

}  // namespace nockvm::discovery
