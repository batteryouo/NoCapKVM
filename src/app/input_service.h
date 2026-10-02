#pragma once
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "input_pump.h"

namespace nockvm::app {

struct AppState;

// Runs Master-side input capture and forwarding on a dedicated thread with
// its own Windows message loop, so WH_MOUSE_LL/WH_KEYBOARD_LL callbacks and
// forwarding to Slave never wait on rendering, buffer swaps, clipboard work
// or anything else on the UI thread. Windows only; elsewhere it starts no
// thread and status() always reports Master ownership.
//
// The referenced server and arrangement must outlive this object.
// Destruction restores local input (resumes and removes the hooks) before
// returning.
class InputService {
public:
  InputService(discovery::TcpServer& server, const topology::ScreenArrangement& arrangement,
               std::vector<display::MonitorInfo> local_monitors);
  ~InputService();
  InputService(const InputService&) = delete;
  InputService& operator=(const InputService&) = delete;

  const discovery::TcpServer* server() const { return &session_.server; }

  // Latest ownership/held-key snapshot published by the input thread.
  InputStatus status() const;

private:
  void run();
  void publish_status();

  InputSession session_;
  void* stop_event_ = nullptr;  // Win32 manual-reset event; signaled to end run()
  mutable std::mutex status_mutex_;
  InputStatus status_;  // guarded by status_mutex_
  std::thread thread_;
};

// Starts or stops state.input_service to match the current role and
// TcpServer. Call once per frame from the UI thread.
void pump_input_service(AppState& state);

}  // namespace nockvm::app
