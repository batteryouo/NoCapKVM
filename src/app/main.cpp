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
#include "nockvm/display/monitor_info.h"
#include "nockvm/input/inject.h"
#include "nockvm/topology/crossing.h"
#include "quit.h"
#include "tray.h"
#include "ui.h"

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#else
#include <X11/Xlib.h>
#endif

namespace {

void glfw_error_callback(int error, const char* description) { std::fprintf(stderr, "GLFW error %d: %s\n", error, description); }

void window_close_callback(GLFWwindow* window) {
  // Closing the window hides it; quitting is handled by the tray action.
  glfwSetWindowShouldClose(window, GLFW_FALSE);
  glfwHideWindow(window);
}

#ifdef _WIN32
WNDPROC g_original_wndproc = nullptr;

// Raw Input supplies unconstrained relative deltas while the input hook suppresses events.
LRESULT CALLBACK raw_input_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_INPUT) {
    UINT size = 0;
    GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
    if (size > 0) {
      std::vector<BYTE> buffer(size);
      if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) ==
          size) {
        const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
        if (raw->header.dwType == RIM_TYPEMOUSE && !(raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
          nockvm::input::feed_raw_delta(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
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
  state.device_id = nockvm::discovery::get_or_create_device_id();
  state.hostname = nockvm::discovery::get_hostname();
  state.local_monitors = nockvm::display::get_local_monitors();
  if (!state.local_monitors.empty()) {
    const auto b = nockvm::topology::compute_bounds(state.local_monitors);
    nockvm::input::configure_pointer_bounds(b.min_x, b.max_x, b.min_y, b.max_y);
  }
  nockvm::app::resume_last_role_if_any(state);

  while (!glfwWindowShouldClose(window) && !nockvm::app::quit_requested()) {
    glfwPollEvents();
    nockvm::app::pump_input(state);
    nockvm::app::pump_audio(state);
    nockvm::app::pump_auto_connect(state);
    nockvm::app::pump_tray();
    nockvm::app::pump_clipboard(state);

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

    glfwSwapBuffers(window);
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
  glfwTerminate();
  log_step("glfwTerminate");
  return 0;
}
