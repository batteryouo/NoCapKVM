#include "input_service.h"
#include <utility>
#include "app_state.h"
#include "nockvm/telemetry/counters.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace nockvm::app {
namespace {

#ifdef _WIN32
// Longest the input thread sleeps with no message before re-running
// pump_input() to notice connection changes. Hook callbacks and raw input
// wake it immediately.
constexpr DWORD kIdleWaitMs = 20;
constexpr wchar_t kSinkClassName[] = L"NoCapKVMInputSink";

// Raw Input supplies unconstrained relative deltas while the input hook suppresses events.
LRESULT CALLBACK sink_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_INPUT) {
    telemetry::counters().raw_input_events.fetch_add(1, std::memory_order_relaxed);
    // RIDEV_INPUTSINK delivers WM_INPUT at full report rate even while
    // nothing is suppressed and feed_raw_delta() would ignore it; skip the
    // query entirely then. raw_input_processed against raw_input_events
    // shows how much of the volume took this path.
    if (input::is_suppressed()) {
      telemetry::counters().raw_input_processed.fetch_add(1, std::memory_order_relaxed);
      RAWINPUT raw{};
      UINT size = sizeof(raw);
      const UINT copied =
          GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));
      if (copied != static_cast<UINT>(-1) && raw.header.dwType == RIM_TYPEMOUSE &&
          !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
        input::feed_raw_delta(raw.data.mouse.lLastX, raw.data.mouse.lLastY);
      }
    }
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

HWND create_sink_window() {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = sink_wndproc;
  wc.hInstance = instance;
  wc.lpszClassName = kSinkClassName;
  RegisterClassExW(&wc);  // fails harmlessly once the class already exists
  return CreateWindowExW(0, kSinkClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
}

void set_raw_mouse_target(HWND hwnd) {
  RAWINPUTDEVICE rid{};
  rid.usUsagePage = 0x01;  // generic desktop
  rid.usUsage = 0x02;      // mouse
  rid.dwFlags = hwnd ? RIDEV_INPUTSINK : RIDEV_REMOVE;
  rid.hwndTarget = hwnd;
  RegisterRawInputDevices(&rid, 1, sizeof(rid));
}
#endif

}  // namespace

InputService::InputService(discovery::TcpServer& server, const topology::ScreenArrangement& arrangement,
                           std::vector<display::MonitorInfo> local_monitors)
    : session_(server, arrangement, std::move(local_monitors)) {
#ifdef _WIN32
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (stop_event_) thread_ = std::thread(&InputService::run, this);
#endif
}

InputService::~InputService() {
#ifdef _WIN32
  if (stop_event_) {
    SetEvent(stop_event_);
    if (thread_.joinable()) thread_.join();
    CloseHandle(stop_event_);
  }
#endif
}

InputStatus InputService::status() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_;
}

void InputService::publish_status() {
  InputStatus current = input_status(session_);
  std::lock_guard<std::mutex> lock(status_mutex_);
  if (!(current == status_)) status_ = std::move(current);
}

void InputService::run() {
#ifdef _WIN32
  const HWND sink = create_sink_window();
  if (sink) set_raw_mouse_target(sink);

  const HANDLE stop = static_cast<HANDLE>(stop_event_);
  MSG msg;
  while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
    // Low-level hook callbacks for this thread run inside PeekMessageW().
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    pump_input(session_);
    publish_status();
    MsgWaitForMultipleObjectsEx(1, &stop, kIdleWaitMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
  }

  deactivate_input(session_);
  publish_status();
  if (sink) {
    set_raw_mouse_target(nullptr);
    DestroyWindow(sink);
  }
#endif
}

void pump_input_service(AppState& state) {
  discovery::TcpServer* server = state.role == discovery::Role::Master ? state.tcp_server.get() : nullptr;
  if (!server) {
    state.input_service.reset();
    return;
  }
  if (state.input_service && state.input_service->server() == server) return;
  state.input_service.reset();
  state.input_service = std::make_unique<InputService>(*server, state.screen_arrangement, state.local_monitors);
}

}  // namespace nockvm::app
