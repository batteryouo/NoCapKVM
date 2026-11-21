#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <thread>
#include <vector>
#include "nockvm/discovery/clipboard_protocol.h"
#include "nockvm/discovery/known_peers.h"
#include "nockvm/discovery/monitor_protocol.h"
#include "nockvm/discovery/platform_socket.h"
#include "nockvm/discovery/tcp_client.h"
#include "nockvm/discovery/tcp_server.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

using namespace nockvm::discovery;
using Clock = std::chrono::steady_clock;

namespace {

void set_env(const char* name, const std::filesystem::path& value) {
#ifdef _WIN32
  _putenv_s(name, value.string().c_str());
#else
  setenv(name, value.string().c_str(), 1);
#endif
}

template <typename Pred>
bool wait_until(Pred pred, std::chrono::milliseconds timeout) {
  const auto deadline = Clock::now() + timeout;
  while (Clock::now() < deadline) {
    if (pred()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return pred();
}

// A loopback TCP proxy for one connection. Bytes from the server to the
// client are forwarded in `chunk`-sized pieces with `delay` between them,
// which simulates a slow link; the other direction is forwarded as is.
class ThrottlingProxy {
public:
  ThrottlingProxy(uint16_t server_port, size_t chunk, std::chrono::milliseconds delay)
      : server_port_(server_port), chunk_(chunk), delay_(delay) {
    listener_ = create_tcp_socket();
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    listen(listener_, 1);
    sockaddr_in bound{};
#ifdef _WIN32
    int len = sizeof(bound);
#else
    socklen_t len = sizeof(bound);
#endif
    getsockname(listener_, reinterpret_cast<sockaddr*>(&bound), &len);
    port_ = ntohs(bound.sin_port);
    accept_thread_ = std::thread([this] { run(); });
  }

  ~ThrottlingProxy() {
    stopping_ = true;
    close_socket(listener_);
    // shutdown() before close() so threads blocked in recv() return on
    // every platform.
    if (client_ != kInvalidSocket) shutdown(client_, 2);
    if (server_ != kInvalidSocket) shutdown(server_, 2);
    accept_thread_.join();
    if (to_client_.joinable()) to_client_.join();
    if (to_server_.joinable()) to_server_.join();
    if (client_ != kInvalidSocket) close_socket(client_);
    if (server_ != kInvalidSocket) close_socket(server_);
  }

  uint16_t port() const { return port_; }

private:
  void run() {
    socket_t client = accept(listener_, nullptr, nullptr);
    if (client == kInvalidSocket || stopping_) return;
    socket_t server = create_tcp_socket();
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port_);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    client_ = client;
    server_ = server;
    to_client_ = std::thread([this] { forward(server_, client_, chunk_, delay_); });
    to_server_ = std::thread([this] { forward(client_, server_, 64 * 1024, std::chrono::milliseconds(0)); });
  }

  void forward(socket_t from, socket_t to, size_t chunk, std::chrono::milliseconds delay) {
    std::vector<uint8_t> buf(chunk);
    while (!stopping_) {
      const int n = recv(from, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
      if (n <= 0) break;
      if (!send_all(to, buf.data(), static_cast<size_t>(n))) break;
      if (delay.count() > 0) std::this_thread::sleep_for(delay);
    }
    shutdown(to, 2);
  }

  uint16_t server_port_;
  size_t chunk_;
  std::chrono::milliseconds delay_;
  socket_t listener_ = kInvalidSocket;
  uint16_t port_ = 0;
  std::atomic<socket_t> client_{kInvalidSocket};
  std::atomic<socket_t> server_{kInvalidSocket};
  std::atomic<bool> stopping_{false};
  std::thread accept_thread_;
  std::thread to_client_;
  std::thread to_server_;
};

}  // namespace

// Real Master/Slave session over loopback. Only non-input message types are
// sent, so nothing is injected into this machine's desktop.
int main() {
  const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "nockvm_test_tcp_session";
  std::filesystem::remove_all(scratch);
  set_env("NOCKVM_HOME", scratch);
  SocketLibraryGuard sockets;

  KnownPeers server_peers;
  KnownPeers client_peers;
  TcpServer server(/*own_device_id=*/1001, server_peers);
  const uint8_t none = 0;
  assert(!server.send_input(kMsgHeartbeat, &none, 0));  // no session yet
  server.start();
  TcpClient client(/*own_device_id=*/2002, "127.0.0.1", server.port(), client_peers);
  client.start();

  assert(wait_until([&] { return server.status().state == ConnectionState::Pairing; }, std::chrono::seconds(5)));
  server.approve_pairing();
  assert(wait_until([&] { return server.status().state == ConnectionState::Connected; }, std::chrono::seconds(5)));
  assert(wait_until([&] { return client.status().state == ConnectionState::Connected; }, std::chrono::seconds(5)));

  // A large bulk payload and the ordered messages queued right behind it
  // all return without waiting for the network, and the bulk payload
  // arrives intact.
  std::vector<uint8_t> big(6u << 20);
  for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i * 31);
  const auto t0 = Clock::now();
  assert(server.send_bulk(kMsgClipboardText, big.data(), big.size()));
  for (int i = 0; i < 100; ++i) assert(server.send_input(kMsgHeartbeat, &none, 0));
  const double queued_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  std::printf("queued 6MiB bulk + 100 ordered messages in %.2f ms\n", queued_ms);

  ClipboardMessage received;
  assert(wait_until([&] { return client.take_pending_clipboard(received); }, std::chrono::seconds(10)));
  assert(received.type == ClipboardMsgType::Text);
  assert(received.data == big);
  assert(server.status().state == ConnectionState::Connected);

  // Disconnecting closes the session's queue.
  server.disconnect_current();
  assert(wait_until([&] { return server.status().state != ConnectionState::Connected; }, std::chrono::seconds(5)));
  assert(!server.send_input(kMsgHeartbeat, &none, 0));

  client.stop();
  server.stop();

  // A frame that takes longer to arrive than both sides' heartbeat timeout
  // must not drop the session: the receiving loop keeps sending heartbeats
  // and counts the arriving bytes as the peer being alive. 3MiB through a
  // proxy forwarding 64KiB every 125ms (about 512KiB/s) takes about 6s,
  // twice the 3s timeouts used here.
  {
    constexpr auto kTimeout = std::chrono::seconds(3);
    TcpServer slow_server(/*own_device_id=*/1001, server_peers, kTimeout);
    slow_server.start();
    ThrottlingProxy proxy(slow_server.port(), 64 * 1024, std::chrono::milliseconds(125));
    TcpClient slow_client(/*own_device_id=*/2002, "127.0.0.1", proxy.port(), client_peers, kTimeout);
    slow_client.start();
    // Trusted since the first session, so no pairing prompt this time.
    assert(wait_until([&] { return slow_server.status().state == ConnectionState::Connected; },
                      std::chrono::seconds(5)));
    assert(wait_until([&] { return slow_client.status().state == ConnectionState::Connected; },
                      std::chrono::seconds(5)));

    std::vector<uint8_t> slow_big(3u << 20);
    for (size_t i = 0; i < slow_big.size(); ++i) slow_big[i] = static_cast<uint8_t>(i * 17);
    const auto start = Clock::now();
    assert(slow_server.send_bulk(kMsgClipboardText, slow_big.data(), slow_big.size()));

    ClipboardMessage slow_received;
    bool stayed_connected = true;
    bool got = false;
    while (Clock::now() - start < std::chrono::seconds(30)) {
      if (slow_server.status().state != ConnectionState::Connected ||
          slow_client.status().state != ConnectionState::Connected) {
        stayed_connected = false;
        break;
      }
      if (slow_client.take_pending_clipboard(slow_received)) {
        got = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const double took_s = std::chrono::duration<double>(Clock::now() - start).count();
    std::printf("slow 3MiB frame: %.1f s, stayed_connected=%d\n", took_s, stayed_connected ? 1 : 0);
    assert(stayed_connected);
    assert(got);
    assert(slow_received.data == slow_big);
    assert(took_s > 2.0 * std::chrono::duration<double>(kTimeout).count() * 0.8);  // the link really was slow

    // Stopping does not wait for anything long-running.
    const auto stop_start = Clock::now();
    slow_client.stop();
    slow_server.stop();
    assert(Clock::now() - stop_start < std::chrono::seconds(3));
  }

  std::filesystem::remove_all(scratch);
  return 0;
}
