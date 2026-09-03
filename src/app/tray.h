#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

struct GLFWwindow;

namespace nockvm::app {

#ifdef _WIN32

// Installs a system tray icon for `window`. Left-click/double-click
// restores the window (glfwShowWindow + focus); right-click opens a context
// menu with Exit. Call once, after the window exists.
void install_tray(GLFWwindow* window);
void uninstall_tray();

// Forwards every message the app's window subclass sees; handles the tray
// icon's callback message and its Exit menu command (which calls
// request_quit(), see quit.h). Caller should still pass msg on to
// CallWindowProc/DefWindowProc regardless of the result.
bool tray_handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

#else

// No tray support yet outside Windows -- closing the window still quits.
inline void install_tray(GLFWwindow*) {}
inline void uninstall_tray() {}

#endif

}  // namespace nockvm::app
