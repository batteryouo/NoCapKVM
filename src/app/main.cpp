#include <chrono>
#include <cstdio>
#include <vector>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include "app_state.h"
#include "audio_pump.h"
#include "auto_connect_pump.h"
#include "clipboard_pump.h"
#include "input_pump.h"
#include "nockvm/discovery/identity.h"
#include "nockvm/discovery/user_settings.h"
#include "nockvm/display/monitor_info.h"
#include "nockvm/input/hook.h"
#include "nockvm/input/inject.h"
#include "nockvm/telemetry/counters.h"
#include "nockvm/telemetry/telemetry.h"
#include "nockvm/topology/crossing.h"
#include "quit.h"
#include "telemetry_pump.h"
#include "tray.h"
#include "ui.h"

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include "app_resource.h"
#else
#include <X11/Xlib.h>
#include "ico_decode.h"
#include "icon_path.h"
#endif

namespace {

void glfw_error_callback(int error, const char* description) { std::fprintf(stderr, "GLFW error %d: %s\n", error, description); }

void window_close_callback(GLFWwindow* window) {
  // Closing the window hides it; quitting is handled by the tray action.
  glfwSetWindowShouldClose(window, GLFW_FALSE);
  glfwHideWindow(window);
}

#ifndef _WIN32
void set_window_icon(GLFWwindow* window) {
  const auto icon = nockvm::app::load_ico_as_argb32(nockvm::app::app_icon_path());
  if (!icon) return;
  std::vector<unsigned char> rgba(icon->argb32_be.size());
  for (size_t i = 0; i < icon->argb32_be.size(); i += 4) {
    rgba[i] = icon->argb32_be[i + 1];
    rgba[i + 1] = icon->argb32_be[i + 2];
    rgba[i + 2] = icon->argb32_be[i + 3];
    rgba[i + 3] = icon->argb32_be[i];
  }
  GLFWimage glfw_icon{icon->width, icon->height, rgba.data()};
  glfwSetWindowIcon(window, 1, &glfw_icon);
}
#endif

#ifdef _WIN32
WNDPROC g_original_wndproc = nullptr;
HICON g_large_window_icon = nullptr;
HICON g_small_window_icon = nullptr;

void set_window_icon(GLFWwindow* window) {
  const HINSTANCE instance = GetModuleHandleW(nullptr);
  const auto resource = MAKEINTRESOURCEW(NOCKVM_APP_ICON_RESOURCE_ID);
  g_large_window_icon = static_cast<HICON>(LoadImageW(instance, resource, IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                                       GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
  g_small_window_icon = static_cast<HICON>(LoadImageW(instance, resource, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                                       GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  const HWND hwnd = glfwGetWin32Window(window);
  if (g_large_window_icon) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_large_window_icon));
  if (g_small_window_icon) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_small_window_icon));
}

void destroy_window_icons() {
  if (g_large_window_icon) DestroyIcon(g_large_window_icon);
  if (g_small_window_icon) DestroyIcon(g_small_window_icon);
  g_large_window_icon = nullptr;
  g_small_window_icon = nullptr;
}

// Raw Input supplies unconstrained relative deltas while the input hook suppresses events.
LRESULT CALLBACK raw_input_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_INPUT) {
    nockvm::telemetry::counters().raw_input_events.fetch_add(1, std::memory_order_relaxed);
    // feed_raw_delta() below is a no-op unless input is currently
    // suppressed (see hook.h) -- RIDEV_INPUTSINK still delivers WM_INPUT at
    // full HID report rate the rest of the time (normally almost always,
    // since suppression only happens while a Slave owns input), so paying
    // for GetRawInputData's allocating query/copy then was pure waste with
    // nothing to show for it. Skip the whole path when unsuppressed;
    // raw_input_processed counts how often it actually runs, so comparing
    // it against raw_input_events in the periodic summary shows how much
    // of the incoming volume is genuinely processed versus cheaply skipped.
    if (nockvm::input::is_suppressed()) {
      nockvm::telemetry::counters().raw_input_processed.fetch_add(1, std::memory_order_relaxed);
      UINT size = 0;
      GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
      if (size > 0) {
        std::vector<BYTE> buffer(size);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, buffer.data(), &size,
                             sizeof(RAWINPUTHEADER)) == size) {
          const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
          if (raw->header.dwType == RIM_TYPEMOUSE && !(raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            nockvm::input::feed_raw_delta(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
          }
        }
      }
    }
  }
  nockvm::app::tray_handle_message(hwnd, msg, wparam, lparam);
  return CallWindowProcW(g_original_wndproc, hwnd, msg, wparam, lparam);
}

void install_raw_input(GLFWwindow* window) {
  const HWND hwnd = glfwGetWin32Window(window);

  RAWINPUTDEVICE rid{};
  rid.usUsagePage = 0x01;  // generic desktop
  rid.usUsage = 0x02;      // mouse
  rid.dwFlags = RIDEV_INPUTSINK;
  rid.hwndTarget = hwnd;
  RegisterRawInputDevices(&rid, 1, sizeof(rid));

  g_original_wndproc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
  SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(raw_input_wndproc));
}

void uninstall_raw_input(GLFWwindow* window) {
  if (!g_original_wndproc) return;
  const HWND hwnd = glfwGetWin32Window(window);
  SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_wndproc));
  g_original_wndproc = nullptr;
}
#endif

}  // namespace

int main() {
#ifndef _WIN32
  // XInitThreads must precede GLFW initialization because Xlib is used from multiple threads.
  XInitThreads();
#endif
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) return 1;

  GLFWwindow* window = glfwCreateWindow(640, 480, "NoCapKVM", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  set_window_icon(window);

#ifdef _WIN32
  install_raw_input(window);
#endif
  glfwSetWindowCloseCallback(window, window_close_callback);
  nockvm::app::install_tray(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 130");

  nockvm::app::AppState state;
  const nockvm::discovery::UserSettings settings = nockvm::discovery::load_user_settings();
  state.connection_timeout_s = settings.connection_timeout_s;
  state.auto_connect_enabled = settings.auto_connect_enabled;
  state.audio_send_enabled = settings.slave_audio_send_enabled;
  state.audio_desired_format = settings.slave_audio_format;
  state.audio_master_overridden = settings.master_audio_overridden;
  state.audio_master_desired_send_enabled = settings.master_audio_send_enabled;
  state.audio_master_desired_format = settings.master_audio_format;
  state.device_id = nockvm::discovery::get_or_create_device_id();
  state.hostname = nockvm::discovery::get_hostname();
  state.local_monitors = nockvm::display::get_local_monitors();
  if (!state.local_monitors.empty()) {
    const auto b = nockvm::topology::compute_bounds(state.local_monitors);
    nockvm::input::configure_pointer_bounds(b.min_x, b.max_x, b.min_y, b.max_y);
  }
  nockvm::app::resume_last_role_if_any(state);

  nockvm::telemetry::init(nockvm::discovery::get_config_dir() / "logs");

  // How long a hidden-window iteration may block in glfwWaitEventsTimeout()
  // before running the pumps below again on its own. Matches the ~60Hz
  // cadence a vsynced visible frame would normally run at, so audio/tray/
  // clipboard pumping stays just as responsive while hidden -- any actual
  // Windows message (including WH_MOUSE_LL/WH_KEYBOARD_LL hook delivery,
  // which requires this thread to be pumping messages promptly) wakes the
  // wait immediately regardless of this timeout.
  constexpr double kHiddenLoopWaitSeconds = 1.0 / 60.0;

  using nockvm::telemetry::FrameSegment;
  while (!glfwWindowShouldClose(window) && !nockvm::app::quit_requested()) {
    const auto frame_start = std::chrono::steady_clock::now();
    const bool window_visible = glfwGetWindowAttrib(window, GLFW_VISIBLE) != 0;
    nockvm::telemetry::FrameSegmentTimes segments{};
    auto segment_start = frame_start;
    auto end_segment = [&](FrameSegment segment) {
      const auto now = std::chrono::steady_clock::now();
      segments[static_cast<size_t>(segment)] = std::chrono::duration<double, std::milli>(now - segment_start).count();
      segment_start = now;
    };

    if (window_visible) {
      glfwPollEvents();
    } else {
      // A hidden window's glfwSwapBuffers() can be throttled by the OS/GPU
      // driver for an unbounded and growing duration (present calls have no
      // reason to hurry when nothing is being displayed), which would stall
      // this thread's message pump and, with it, WH_MOUSE_LL/WH_KEYBOARD_LL
      // hook delivery -- Windows only calls a low-level hook proc while its
      // owning thread is promptly pumping messages, so a stalled pump here
      // delays the physical mouse/keyboard system-wide, not just this app's
      // own rendering. Skip rendering entirely while hidden (see below) and
      // wait on the message queue instead of spinning, so this thread stays
      // idle-but-responsive rather than either busy-looping or blocking in
      // a throttled present call.
      glfwWaitEventsTimeout(kHiddenLoopWaitSeconds);
    }
    end_segment(FrameSegment::Events);

    nockvm::app::pump_input(state);
    end_segment(FrameSegment::Input);
    nockvm::app::pump_audio(state);
    end_segment(FrameSegment::Audio);
    nockvm::app::pump_auto_connect(state);
    end_segment(FrameSegment::AutoConnect);
    nockvm::app::pump_tray();
    end_segment(FrameSegment::Tray);
    nockvm::app::pump_clipboard(state);
    end_segment(FrameSegment::Clipboard);

    if (!window_visible) {
      const double frame_time_ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_start).count();
      nockvm::app::pump_telemetry(state, frame_time_ms, false, segments);
      continue;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    switch (state.screen) {
      case nockvm::app::Screen::RoleSelect: nockvm::app::draw_role_select(state); break;
      case nockvm::app::Screen::Discovery: nockvm::app::draw_discovery(state); break;
      case nockvm::app::Screen::ManageDevices: nockvm::app::draw_manage_devices(state); break;
      case nockvm::app::Screen::Arrangement: nockvm::app::draw_arrangement(state); break;
    }

    ImGui::Render();
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    end_segment(FrameSegment::Render);

    glfwSwapBuffers(window);
    end_segment(FrameSegment::Swap);

    const double frame_time_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_start).count();
    nockvm::app::pump_telemetry(state, frame_time_ms, true, segments);
  }

  // Emit per-step shutdown timing for diagnosing close-time stalls.
  const auto shutdown_start = std::chrono::steady_clock::now();
  auto log_step = [&](const char* label) {
    const auto now = std::chrono::steady_clock::now();
    std::fprintf(stderr, "[shutdown] %s: %lld ms\n", label,
                 static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now - shutdown_start).count()));
  };

  state.announcer.reset();
  log_step("announcer.reset");
  state.listener.reset();
  log_step("listener.reset");
  state.tcp_server.reset();
  log_step("tcp_server.reset");
  state.tcp_client.reset();
  log_step("tcp_client.reset");

  ImGui_ImplOpenGL3_Shutdown();
  log_step("ImGui_ImplOpenGL3_Shutdown");
  ImGui_ImplGlfw_Shutdown();
  log_step("ImGui_ImplGlfw_Shutdown");
  ImGui::DestroyContext();
  log_step("ImGui::DestroyContext");

#ifdef _WIN32
  uninstall_raw_input(window);
  log_step("uninstall_raw_input");
#endif
  nockvm::app::uninstall_tray();
  log_step("uninstall_tray");

  glfwDestroyWindow(window);
  log_step("glfwDestroyWindow");
#ifdef _WIN32
  destroy_window_icons();
#endif
  glfwTerminate();
  log_step("glfwTerminate");
  return 0;
}
