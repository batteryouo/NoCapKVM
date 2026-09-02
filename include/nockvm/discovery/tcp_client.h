#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include "nockvm/discovery/connection_types.h"
#include "nockvm/discovery/known_peers.h"
#include "nockvm/discovery/noise_primitives.h"

namespace nockvm::discovery {

class SecureChannel;

// Connects out to a single TCP server. On an unknown peer, runs TOFU
// pairing (fingerprint display, waits for the Master's approve/reject
// decision) before proceeding; on an already-trusted peer, skips straight
// to the Noise IK handshake. Then watches the connection until it drops
// or stop() is called.
class TcpClient {
public:
  TcpClient(uint64_t own_device_id, std::string peer_ip, uint16_t peer_port, KnownPeers& known_peers);
  ~TcpClient();
  TcpClient(const TcpClient&) = delete;
  TcpClient& operator=(const TcpClient&) = delete;

  void start();
  void stop();

  ConnectionInfo status() const;

  // Called from the UI/audio thread to send a message on the active
  // connection (e.g. kMsgAudioStatus, when the user changes the audio
  // quality setting). No-op (returns false) if there is no connection
  // currently in the Connected state. Mirrors TcpServer::send_input's
  // concurrency reasoning: send/receive use independent SecureChannel
  // nonce counters, and this is the only caller of send(), so there's no
  // send/send race beyond send_mutex_ serializing this method against
  // itself.
  bool send_message(uint8_t msg_type, const uint8_t* payload, size_t len);

private:
  // Outcome of a single connect-through-disconnect attempt, used by run()
  // to decide whether to retry and how long to back off before doing so.
  enum class AttemptOutcome {
    kRetryFast,     // reached Connected before failing -- retry soon
    kRetryBackoff,  // never got connected -- retry after a growing delay
    kGiveUp,        // pairing was explicitly rejected -- not worth retrying
  };
  AttemptOutcome run_once();
  void run();

  uint64_t own_device_id_;
  Keypair own_static_;
  KnownPeers& known_peers_;
  std::string peer_ip_;
  uint16_t peer_port_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  mutable std::mutex status_mutex_;
  ConnectionInfo status_;
  mutable std::mutex send_mutex_;
  SecureChannel* active_channel_ = nullptr;  // set while Connected, guarded by send_mutex_
};

}  // namespace nockvm::discovery
