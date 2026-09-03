#ifdef _WIN32

#include "tray.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <shellapi.h>

namespace nockvm::app {

namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT_PTR kExitMenuCommand = 1;

NOTIFYICONDATAW g_nid{};
GLFWwindow* g_window = nullptr;
bool g_quit_requested = false;

void restore_window() {
  if (!g_window) return;
  glfwShowWindow(g_window);
  glfwFocusWindow(g_window);
}

void show_context_menu(HWND hwnd) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kExitMenuCommand, L"結束 Exit");
  POINT pt;
  GetCursorPos(&pt);
  // Without this the popup can fail to dismiss itself on an outside click --
  // a documented TrackPopupMenu quirk, not optional polish.
  SetForegroundWindow(hwnd);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
}

}  // namespace

void install_tray(GLFWwindow* window) {
  g_window = window;
  const HWND hwnd = glfwGetWin32Window(window);

  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = hwnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g_nid.uCallbackMessage = kTrayCallbackMessage;
  // IDI_APPLICATION resolves to the ANSI MAKEINTRESOURCEA in this
  // (non-UNICODE-defined) build; spell out the wide resource id directly
  // so it matches LoadIconW's LPCWSTR parameter.
  g_nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
  lstrcpynW(g_nid.szTip, L"NoCapKVM", ARRAYSIZE(g_nid.szTip));
  Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void uninstall_tray() {
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
  g_window = nullptr;
}

bool tray_handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == kTrayCallbackMessage) {
    // NOTIFYICONDATA's default (unversioned) callback ABI: lParam carries
    // the raw mouse message value directly, not packed into LOWORD.
    if (lparam == WM_LBUTTONUP || lparam == WM_LBUTTONDBLCLK) {
      restore_window();
    } else if (lparam == WM_RBUTTONUP) {
      show_context_menu(hwnd);
    }
    return true;
  }
  if (msg == WM_COMMAND && LOWORD(wparam) == kExitMenuCommand) {
    g_quit_requested = true;
    return true;
  }
  return false;
}

bool tray_quit_requested() { return g_quit_requested; }

}  // namespace nockvm::app

#endif
