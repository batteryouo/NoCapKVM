#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <vector>
#include "nockvm/discovery/clipboard_protocol.h"
#include "nockvm/discovery/known_peers.h"
#include "nockvm/discovery/monitor_protocol.h"
#include "nockvm/discovery/platform_socket.h"
#include "nockvm/discovery/tcp_client.h"
#include "nockvm/discovery/tcp_server.h"

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
  std::filesystem::remove_all(scratch);
  return 0;
}
