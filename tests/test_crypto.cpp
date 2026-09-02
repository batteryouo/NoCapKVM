#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include "nockvm/discovery/known_peers.h"
#include "nockvm/discovery/noise_ik.h"
#include "nockvm/discovery/pairing.h"
#include "nockvm/discovery/platform_socket.h"
#include "nockvm/discovery/secure_channel.h"
#include "nockvm/discovery/static_keys.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

using namespace nockvm::discovery;

namespace {

void set_env(const char* name, const std::filesystem::path& value) {
#ifdef _WIN32
  _putenv_s(name, value.string().c_str());
#else
  setenv(name, value.string().c_str(), 1);
#endif
}

socket_t make_loopback_listener(uint16_t& port_out) {
  socket_t sock = create_tcp_socket();
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  listen(sock, 1);
  sockaddr_in bound{};
#ifdef _WIN32
  int len = sizeof(bound);
#else
  socklen_t len = sizeof(bound);
#endif
  getsockname(sock, reinterpret_cast<sockaddr*>(&bound), &len);
  port_out = ntohs(bound.sin_port);
  return sock;
}

socket_t connect_loopback(uint16_t port) {
  socket_t sock = create_tcp_socket();
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  return sock;
}

}  // namespace

int main() {
  // Fingerprint determinism + sensitivity to either input.
  {
    Key32 a{}, b{}, c{};
    a.fill(0x11);
    b.fill(0x22);
    c.fill(0x33);
    const std::string fp1 = compute_fingerprint(a, b);
    assert(compute_fingerprint(a, b) == fp1);
    assert(fp1.size() == 6);
    assert(compute_fingerprint(a, c) != fp1);
    assert(compute_fingerprint(c, b) != fp1);
  }

  // Real loopback-socket IK round trip.
  {
    const Keypair initiator_static = generate_keypair();
    const Keypair responder_static = generate_keypair();

    uint16_t port = 0;
    socket_t listener = make_loopback_listener(port);

    IkResult responder_result;
    std::thread responder_thread([&] {
      socket_t client = accept(listener, nullptr, nullptr);
      set_receive_timeout(client, std::chrono::milliseconds(200));
      responder_result =
          run_ik_responder(client, responder_static, std::chrono::steady_clock::now() + std::chrono::seconds(3));
      close_socket(client);
    });

    socket_t initiator_sock = connect_loopback(port);
    set_receive_timeout(initiator_sock, std::chrono::milliseconds(200));
    const IkResult initiator_result =
        run_ik_initiator(initiator_sock, initiator_static, responder_static.public_key,
                          std::chrono::steady_clock::now() + std::chrono::seconds(3));
    responder_thread.join();
    close_socket(initiator_sock);
    close_socket(listener);

    assert(initiator_result.ok);
    assert(responder_result.ok);
    assert(initiator_result.keys.send_key == responder_result.keys.recv_key);
    assert(initiator_result.keys.recv_key == responder_result.keys.send_key);
    assert(responder_result.peer_static == initiator_static.public_key);
  }

  // Negative test: initiator holds the wrong peer_static -> handshake must fail.
  {
    const Keypair initiator_static = generate_keypair();
    const Keypair responder_static = generate_keypair();
    const Keypair wrong_static = generate_keypair();

    uint16_t port = 0;
    socket_t listener = make_loopback_listener(port);

    IkResult responder_result;
    std::thread responder_thread([&] {
      socket_t client = accept(listener, nullptr, nullptr);
      set_receive_timeout(client, std::chrono::milliseconds(200));
      responder_result =
          run_ik_responder(client, responder_static, std::chrono::steady_clock::now() + std::chrono::seconds(3));
      close_socket(client);
    });

    socket_t initiator_sock = connect_loopback(port);
    set_receive_timeout(initiator_sock, std::chrono::milliseconds(200));
    const IkResult initiator_result =
        run_ik_initiator(initiator_sock, initiator_static, wrong_static.public_key,
                          std::chrono::steady_clock::now() + std::chrono::seconds(3));
    responder_thread.join();
    close_socket(initiator_sock);
    close_socket(listener);

    assert(!initiator_result.ok);
  }

  // SecureChannel round trip over a real loopback socket pair, keyed by a
  // real IK handshake's derived TransportKeys.
  {
    const Keypair initiator_static = generate_keypair();
    const Keypair responder_static = generate_keypair();

    uint16_t port = 0;
    socket_t listener = make_loopback_listener(port);

    IkResult responder_result;
    std::thread responder_thread([&] {
      socket_t client = accept(listener, nullptr, nullptr);
      set_receive_timeout(client, std::chrono::milliseconds(200));
      responder_result =
          run_ik_responder(client, responder_static, std::chrono::steady_clock::now() + std::chrono::seconds(3));
      if (responder_result.ok) {
        SecureChannel channel(client, responder_result.keys);
        uint8_t type = 0;
        std::vector<uint8_t> payload;
        const auto result =
            channel.receive(type, payload, std::chrono::steady_clock::now() + std::chrono::seconds(3));
        assert(result == SecureChannel::RecvResult::Ok);
        assert(type == 7);
        assert(payload == std::vector<uint8_t>({1, 2, 3}));
        const uint8_t reply[1] = {9};
        assert(channel.send(8, reply, 1));
      }
      close_socket(client);
    });

    socket_t initiator_sock = connect_loopback(port);
    set_receive_timeout(initiator_sock, std::chrono::milliseconds(200));
    const IkResult initiator_result =
        run_ik_initiator(initiator_sock, initiator_static, responder_static.public_key,
                          std::chrono::steady_clock::now() + std::chrono::seconds(3));
    assert(initiator_result.ok);

    SecureChannel channel(initiator_sock, initiator_result.keys);
    const uint8_t msg[3] = {1, 2, 3};
    assert(channel.send(7, msg, 3));

    uint8_t type = 0;
    std::vector<uint8_t> payload;
    const auto result = channel.receive(type, payload, std::chrono::steady_clock::now() + std::chrono::seconds(3));
    assert(result == SecureChannel::RecvResult::Ok);
    assert(type == 8);
    assert(payload.size() == 1 && payload[0] == 9);

    responder_thread.join();
    close_socket(initiator_sock);
    close_socket(listener);
  }

  // SecureChannel: mismatched keys must fail closed (Error), not crash or
  // silently decrypt garbage.
  {
    const Keypair initiator_static = generate_keypair();
    const Keypair responder_static = generate_keypair();

    uint16_t port = 0;
    socket_t listener = make_loopback_listener(port);

    IkResult responder_result;
    std::thread responder_thread([&] {
      socket_t client = accept(listener, nullptr, nullptr);
      set_receive_timeout(client, std::chrono::milliseconds(200));
      responder_result =
          run_ik_responder(client, responder_static, std::chrono::steady_clock::now() + std::chrono::seconds(3));
      if (responder_result.ok) {
        // Deliberately swap send/recv keys to simulate a corrupted/mismatched channel.
        TransportKeys wrong_keys{responder_result.keys.recv_key, responder_result.keys.send_key};
        SecureChannel channel(client, wrong_keys);
        uint8_t type = 0;
        std::vector<uint8_t> payload;
        const auto result =
            channel.receive(type, payload, std::chrono::steady_clock::now() + std::chrono::seconds(1));
        assert(result == SecureChannel::RecvResult::Error);
      }
      close_socket(client);
    });

    socket_t initiator_sock = connect_loopback(port);
    set_receive_timeout(initiator_sock, std::chrono::milliseconds(200));
    const IkResult initiator_result =
        run_ik_initiator(initiator_sock, initiator_static, responder_static.public_key,
                          std::chrono::steady_clock::now() + std::chrono::seconds(3));
    assert(initiator_result.ok);

    SecureChannel channel(initiator_sock, initiator_result.keys);
    const uint8_t msg[1] = {42};
    assert(channel.send(1, msg, 1));

    responder_thread.join();
    close_socket(initiator_sock);
    close_socket(listener);
  }

  // Static keypair persistence.
  {
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "nockvm_test_static_keys";
    std::filesystem::remove_all(scratch);
    set_env("NOCKVM_HOME", scratch);
    const Keypair first = get_or_create_static_keypair();
    const Keypair second = get_or_create_static_keypair();
    assert(first.public_key == second.public_key);
    assert(first.private_key == second.private_key);
    std::filesystem::remove_all(scratch);
  }

  // KnownPeers persistence.
  {
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "nockvm_test_known_peers";
    std::filesystem::remove_all(scratch);
    set_env("NOCKVM_HOME", scratch);

    Key32 pubkey{};
    pubkey.fill(0x42);
    {
      KnownPeers peers;
      assert(!peers.is_known(999));
      peers.remember(999, pubkey);
      assert(peers.is_known(999));
    }
    {
      KnownPeers peers2;
      const auto found = peers2.get_pubkey(999);
      assert(found.has_value());
      assert(*found == pubkey);
    }
    std::filesystem::remove_all(scratch);
  }

  // KnownPeers list()/forget().
  {
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "nockvm_test_known_peers_forget";
    std::filesystem::remove_all(scratch);
    set_env("NOCKVM_HOME", scratch);

    Key32 key_a{}, key_b{};
    key_a.fill(0x11);
    key_b.fill(0x22);

    KnownPeers peers;
    peers.remember(1, key_a);
    peers.remember(2, key_b);
    assert(peers.list().size() == 2);

    peers.forget(1);
    assert(!peers.is_known(1));
    assert(peers.is_known(2));
    const auto listed = peers.list();
    assert(listed.size() == 1);
    assert(listed[0].device_id == 2);
    assert(listed[0].pubkey == key_b);

    // Forgetting must also persist: a fresh instance should not see it either.
    KnownPeers peers2;
    assert(!peers2.is_known(1));
    assert(peers2.is_known(2));

    std::filesystem::remove_all(scratch);
  }

  std::printf("All crypto tests passed.\n");
  return 0;
}
