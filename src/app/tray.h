#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

struct GLFWwindow;

namespace nockvm::app {

// Installs the tray icon for `window`. Left-click/double-click restores the
// window (glfwShowWindow + focus). Right-click: Windows opens a context
// menu with Exit; Linux's StatusNotifierItem has no menu and just quits
// directly on right-click (its ContextMenu callback). Call once, after the
// window exists.
void install_tray(GLFWwindow* window);
void uninstall_tray();

// Call once per frame from the main loop. Windows: no-op (tray events
// arrive through the window's own message pump, see tray_handle_message
// below). Linux: non-blocking drain of pending StatusNotifierItem D-Bus
// calls -- Activate/ContextMenu run synchronously from here, on the main
// thread, so they can call GLFW functions directly without needing to hand
// off across threads.
void pump_tray();

#ifdef _WIN32
// Forwards every message the app's window subclass sees; handles the tray
// icon's callback message and its Exit menu command (which calls
// request_quit(), see quit.h). Caller should still pass msg on to
// CallWindowProc/DefWindowProc regardless of the result.
bool tray_handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
#endif

}  // namespace nockvm::app
