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
  // Treat the X button as "hide to tray", not "quit" -- only the tray's
  // own quit trigger (request_quit(), see quit.h) ends the process.
  glfwSetWindowShouldClose(window, GLFW_FALSE);
  glfwHideWindow(window);
}

#ifdef _WIN32
WNDPROC g_original_wndproc = nullptr;

// Master's capture (nockvm/input/hook.cpp) needs raw, unclamped mouse
// deltas while input is suppressed -- WH_MOUSE_LL's own position field is
// clamped to the real desktop bounds, which is exactly the problem this
// exists to work around. Raw Input reports HID deltas directly, bypassing
// that clamp entirely (the same mechanism games use for unbounded
// mouse-look). This subclasses the GLFW window to observe WM_INPUT;
// everything else is forwarded to GLFW's own procedure untouched.
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
  // Xlib is not thread-safe by default. This process makes Xlib calls
  // from two different threads on Linux -- nockvm::display's monitor
  // polling (on TcpClient's background thread, see tcp_client.cpp's
  // periodic get_local_monitors() re-check) and nockvm::clipboard's X11
  // implementation (driven from this thread by clipboard_pump.cpp) --
  // without ever having declared that. XInitThreads() must be called
  // before any other Xlib call in the process, GLFW's own X11 backend's
  // internal calls during glfwInit() included, hence right here first.
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

  state.announcer.reset();
  state.listener.reset();
  state.tcp_server.reset();
  state.tcp_client.reset();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

#ifdef _WIN32
  uninstall_raw_input(window);
#endif
  nockvm::app::uninstall_tray();

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
