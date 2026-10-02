#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <thread>
#include "nockvm/discovery/monitor_protocol.h"
#include "nockvm/discovery/outgoing_queue.h"
#include "nockvm/discovery/ping_protocol.h"
#include "nockvm/discovery/protocol.h"
#include "nockvm/discovery/user_settings.h"

using namespace nockvm::discovery;
using nockvm::display::MonitorInfo;

namespace {

void set_env(const char* name, const std::filesystem::path& value) {
#ifdef _WIN32
  _putenv_s(name, value.string().c_str());
#else
  setenv(name, value.string().c_str(), 1);
#endif
}

}  // namespace

int main() {
  // User settings persist every user-configurable connection and audio choice.
  {
    const std::filesystem::path scratch = std::filesystem::temp_directory_path() / "nockvm_test_user_settings";
    std::filesystem::remove_all(scratch);
    set_env("NOCKVM_HOME", scratch);

    UserSettings settings;
    settings.connection_timeout_s = 45;
    settings.auto_connect_enabled = false;
    settings.slave_audio_send_enabled = false;
    settings.slave_audio_format = {24000, 8};
    settings.master_audio_overridden = true;
    settings.master_audio_send_enabled = false;
    settings.master_audio_format = {24000, 8};
    save_user_settings(settings);

    const UserSettings loaded = load_user_settings();
    assert(loaded.connection_timeout_s == 45);
    assert(!loaded.auto_connect_enabled);
    assert(!loaded.slave_audio_send_enabled);
    assert(loaded.slave_audio_format == settings.slave_audio_format);
    assert(loaded.master_audio_overridden);
    assert(!loaded.master_audio_send_enabled);
    assert(loaded.master_audio_format == settings.master_audio_format);

    std::ofstream out(scratch / "user_settings");
    out << "connection_timeout_s 0\nslave_audio 1 12345 32\nmaster_audio 1 1 48000 16\n";
    out.close();
    const UserSettings recovered = load_user_settings();
    assert(recovered.connection_timeout_s == 10);
    assert(recovered.slave_audio_send_enabled);
    assert(recovered.slave_audio_format == nockvm::audio::AudioFormat{});
    assert(recovered.master_audio_overridden);
    assert(recovered.master_audio_send_enabled);
    assert(recovered.master_audio_format == nockvm::audio::AudioFormat{});

    std::filesystem::remove_all(scratch);
  }

  // Round-trip: Master
  {
    const AnnouncePacket packet = encode_announce(0x0123456789ABCDEFULL, Role::Master, 4242, "master-pc");
    DecodedAnnounce decoded;
    assert(decode_announce(packet.data(), packet.size(), decoded));
    assert(decoded.device_id == 0x0123456789ABCDEFULL);
    assert(decoded.role == Role::Master);
    assert(decoded.tcp_port == 4242);
    assert(decoded.hostname == "master-pc");
  }

  // Round-trip: Slave, zero device id and port
  {
    const AnnouncePacket packet = encode_announce(0, Role::Slave, 0, "slave-1");
    DecodedAnnounce decoded;
    assert(decode_announce(packet.data(), packet.size(), decoded));
    assert(decoded.device_id == 0);
    assert(decoded.role == Role::Slave);
    assert(decoded.tcp_port == 0);
    assert(decoded.hostname == "slave-1");
  }

  // Hostname longer than kMaxHostnameLen is truncated, not overflowed
  {
    const std::string long_name(64, 'x');
    const AnnouncePacket packet = encode_announce(1, Role::Master, 1, long_name);
    DecodedAnnounce decoded;
    assert(decode_announce(packet.data(), packet.size(), decoded));
    assert(decoded.hostname.size() == kMaxHostnameLen);
  }

  // Rejection: wrong length
  {
    const AnnouncePacket packet = encode_announce(1, Role::Master, 1, "x");
    DecodedAnnounce decoded;
    assert(!decode_announce(packet.data(), packet.size() - 1, decoded));
  }

  // Rejection: bad magic
  {
    AnnouncePacket packet = encode_announce(1, Role::Master, 1, "x");
    packet[0] ^= 0xFF;
    DecodedAnnounce decoded;
    assert(!decode_announce(packet.data(), packet.size(), decoded));
  }

  // Rejection: bad version
  {
    AnnouncePacket packet = encode_announce(1, Role::Master, 1, "x");
    packet[5] = 0xFF;
    DecodedAnnounce decoded;
    assert(!decode_announce(packet.data(), packet.size(), decoded));
  }

  // Rejection: bad role byte
  {
    AnnouncePacket packet = encode_announce(1, Role::Master, 1, "x");
    packet[14] = 0xFF;
    DecodedAnnounce decoded;
    assert(!decode_announce(packet.data(), packet.size(), decoded));
  }

  // Device id round-trip
  {
    assert(decode_device_id(encode_device_id(0)) == 0);
    assert(decode_device_id(encode_device_id(UINT64_MAX)) == UINT64_MAX);
    assert(decode_device_id(encode_device_id(0x0123456789ABCDEFULL)) == 0x0123456789ABCDEFULL);
  }

  // Monitor list round-trip: single monitor
  {
    MonitorInfo m;
    m.x = 0;
    m.y = 0;
    m.width = 1920;
    m.height = 1080;
    m.primary = true;
    m.name = "\\\\.\\DISPLAY1";

    const std::vector<uint8_t> encoded = encode_monitor_list({m});
    std::vector<MonitorInfo> decoded;
    assert(decode_monitor_list(encoded.data(), encoded.size(), decoded));
    assert(decoded.size() == 1);
    assert(decoded[0].x == m.x && decoded[0].y == m.y);
    assert(decoded[0].width == m.width && decoded[0].height == m.height);
    assert(decoded[0].primary == m.primary);
    assert(decoded[0].name == m.name);
  }

  // Monitor list round-trip: multiple monitors, including negative
  // coordinates (a monitor placed left of / above the primary).
  {
    MonitorInfo primary;
    primary.x = 0;
    primary.y = 0;
    primary.width = 1920;
    primary.height = 1080;
    primary.primary = true;
    primary.name = "eDP-1";

    MonitorInfo left;
    left.x = -1920;
    left.y = 0;
    left.width = 1920;
    left.height = 1080;
    left.primary = false;
    left.name = "HDMI-1";

    MonitorInfo above;
    above.x = 0;
    above.y = -1200;
    above.width = 2560;
    above.height = 1200;
    above.primary = false;
    above.name = "DP-2";

    const std::vector<MonitorInfo> monitors = {primary, left, above};
    const std::vector<uint8_t> encoded = encode_monitor_list(monitors);
    std::vector<MonitorInfo> decoded;
    assert(decode_monitor_list(encoded.data(), encoded.size(), decoded));
    assert(decoded.size() == 3);
    for (size_t i = 0; i < 3; ++i) {
      assert(decoded[i].x == monitors[i].x && decoded[i].y == monitors[i].y);
      assert(decoded[i].width == monitors[i].width && decoded[i].height == monitors[i].height);
      assert(decoded[i].primary == monitors[i].primary);
      assert(decoded[i].name == monitors[i].name);
    }
  }

  // Monitor list round-trip: empty list
  {
    const std::vector<uint8_t> encoded = encode_monitor_list({});
    std::vector<MonitorInfo> decoded;
    assert(decode_monitor_list(encoded.data(), encoded.size(), decoded));
    assert(decoded.empty());
  }

  // Rejection: truncated payload
  {
    MonitorInfo m;
    m.width = 100;
    m.height = 100;
    m.name = "x";
    const std::vector<uint8_t> encoded = encode_monitor_list({m});
    std::vector<MonitorInfo> decoded;
    assert(!decode_monitor_list(encoded.data(), encoded.size() - 1, decoded));
  }

  // Ping/pong round-trip: the same wire shape serves both message types --
  // a decoded seq matches whatever was encoded, including edge values.
  {
    const std::vector<uint8_t> encoded = encode_ping_seq(0);
    uint32_t seq = 42;
    assert(decode_ping_seq(encoded.data(), encoded.size(), seq));
    assert(seq == 0);
  }
  {
    const std::vector<uint8_t> encoded = encode_ping_seq(0xDEADBEEF);
    uint32_t seq = 0;
    assert(decode_ping_seq(encoded.data(), encoded.size(), seq));
    assert(seq == 0xDEADBEEF);
  }
  {
    const std::vector<uint8_t> encoded = encode_ping_seq(UINT32_MAX);
    uint32_t seq = 0;
    assert(decode_ping_seq(encoded.data(), encoded.size(), seq));
    assert(seq == UINT32_MAX);
    assert(encoded.size() == 4);
  }

  // Rejection: truncated ping/pong payload
  {
    const std::vector<uint8_t> encoded = encode_ping_seq(123);
    uint32_t seq = 0;
    assert(!decode_ping_seq(encoded.data(), encoded.size() - 1, seq));
  }

  // OutgoingQueue: refuses pushes until opened, sends ordered messages in
  // order and ahead of bulk, keeps only the latest bulk message, and
  // reports overflow without queuing.
  {
    OutgoingQueue q(/*max_ordered=*/2);
    const uint8_t a = 1, b = 2, c = 3;
    assert(q.push(10, &a, 1) == OutgoingQueue::PushResult::Closed);
    q.open();
    assert(q.push_bulk(50, &a, 1) == OutgoingQueue::PushResult::Ok);
    assert(q.push_bulk(51, &b, 1) == OutgoingQueue::PushResult::Ok);
    assert(q.push(10, &a, 1) == OutgoingQueue::PushResult::Ok);
    assert(q.push(11, &b, 1) == OutgoingQueue::PushResult::Ok);
    assert(q.push(12, &c, 1) == OutgoingQueue::PushResult::Overflow);
    auto m = q.pop();
    assert(m && m->type == 10 && m->payload == std::vector<uint8_t>{1});
    m = q.pop();
    assert(m && m->type == 11);
    m = q.pop();
    assert(m && m->type == 51 && m->payload == std::vector<uint8_t>{2});
    // Reopening discards leftovers from the previous connection.
    assert(q.push(13, &c, 1) == OutgoingQueue::PushResult::Ok);
    q.open();
    assert(q.push(14, &c, 1) == OutgoingQueue::PushResult::Ok);
    m = q.pop();
    assert(m && m->type == 14);
  }

  // OutgoingQueue: push_latest() overwrites only a same-type message at the
  // tail, so it never reorders against other message types.
  {
    OutgoingQueue q;
    q.open();
    const uint8_t p1 = 1, p2 = 2, p3 = 3, key = 9;
    assert(q.push_latest(20, &p1, 1) == OutgoingQueue::PushResult::Ok);
    assert(q.push_latest(20, &p2, 1) == OutgoingQueue::PushResult::Ok);
    assert(q.push(21, &key, 1) == OutgoingQueue::PushResult::Ok);
    assert(q.push_latest(20, &p3, 1) == OutgoingQueue::PushResult::Ok);
    auto m = q.pop();
    assert(m && m->type == 20 && m->payload == std::vector<uint8_t>{2});
    m = q.pop();
    assert(m && m->type == 21);
    m = q.pop();
    assert(m && m->type == 20 && m->payload == std::vector<uint8_t>{3});
  }

  // OutgoingQueue: close() wakes a writer blocked in pop().
  {
    OutgoingQueue q;
    q.open();
    std::optional<OutgoingMessage> result{OutgoingMessage{}};
    std::thread writer([&] { result = q.pop(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.close();
    writer.join();
    assert(!result.has_value());
    const uint8_t a = 1;
    assert(q.push(10, &a, 1) == OutgoingQueue::PushResult::Closed);
  }

  return 0;
}
