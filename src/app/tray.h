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
// icon's callback message and its Exit menu command. Caller should still
// pass msg on to CallWindowProc/DefWindowProc regardless of the result.
bool tray_handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

// True once the tray's Exit menu item has been chosen; the main loop should
// treat this the same as glfwWindowShouldClose().
bool tray_quit_requested();

#else

// No tray support yet outside Windows -- closing the window still quits.
inline void install_tray(GLFWwindow*) {}
inline void uninstall_tray() {}
inline bool tray_quit_requested() { return false; }

#endif

}  // namespace nockvm::app
