#include "nockvm/discovery/platform_socket.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace nockvm::discovery {
namespace {

socket_t create_socket(int type) {
  static SocketLibraryGuard guard;
  return socket(AF_INET, type, 0);
}

void set_non_blocking(socket_t sock, bool non_blocking) {
#ifdef _WIN32
  u_long mode = non_blocking ? 1 : 0;
  ioctlsocket(sock, FIONBIO, &mode);
#else
  const int flags = fcntl(sock, F_GETFL, 0);
  fcntl(sock, F_SETFL, non_blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
}

}  // namespace

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

socket_t create_udp_socket() { return create_socket(SOCK_DGRAM); }
socket_t create_tcp_socket() { return create_socket(SOCK_STREAM); }

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

bool wait_readable(socket_t sock, std::chrono::milliseconds timeout) {
  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET(sock, &read_set);
  struct timeval tv;
  tv.tv_sec = static_cast<long>(timeout.count() / 1000);
  tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
  return select(static_cast<int>(sock) + 1, &read_set, nullptr, nullptr, &tv) > 0;
}

bool connect_with_timeout(socket_t sock, const sockaddr* addr, size_t addr_len, std::chrono::milliseconds timeout) {
  set_non_blocking(sock, true);

#ifdef _WIN32
  const int rc = connect(sock, addr, static_cast<int>(addr_len));
  const bool in_progress = rc != 0 && WSAGetLastError() == WSAEWOULDBLOCK;
#else
  const int rc = connect(sock, addr, static_cast<socklen_t>(addr_len));
  const bool in_progress = rc != 0 && errno == EINPROGRESS;
#endif

  bool connected = rc == 0;
  if (!connected && !in_progress) {
    set_non_blocking(sock, false);
    return false;
  }

  if (!connected) {
    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(sock, &write_set);
    struct timeval tv;
    tv.tv_sec = static_cast<long>(timeout.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    if (select(static_cast<int>(sock) + 1, nullptr, &write_set, nullptr, &tv) <= 0) {
      set_non_blocking(sock, false);
      return false;  // timed out or select error
    }
    int so_error = 0;
#ifdef _WIN32
    int err_len = sizeof(so_error);
#else
    socklen_t err_len = sizeof(so_error);
#endif
    getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &err_len);
    connected = so_error == 0;
  }

  set_non_blocking(sock, false);
  return connected;
}

bool send_all(socket_t sock, const uint8_t* data, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    const int n = send(sock, reinterpret_cast<const char*>(data + sent), static_cast<int>(len - sent), 0);
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

bool recv_all(socket_t sock, uint8_t* buf, size_t len, std::chrono::steady_clock::time_point deadline) {
  size_t received = 0;
  while (received < len) {
    if (std::chrono::steady_clock::now() >= deadline) return false;
    const int n = recv(sock, reinterpret_cast<char*>(buf + received), static_cast<int>(len - received), 0);
    if (n > 0) received += static_cast<size_t>(n);
    else if (n == 0) return false;  // peer closed
    // n < 0: likely a receive-timeout expiring; loop back and recheck the deadline.
  }
  return true;
}

}  // namespace nockvm::discovery
