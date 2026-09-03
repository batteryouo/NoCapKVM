#pragma once

namespace nockvm::app {

// Set by any quit trigger -- Windows tray's Exit menu item, Linux tray's
// right-click, or the UI's own Exit button (now that the window's own X
// button just hides to the tray instead of quitting). The main loop treats
// this the same as glfwWindowShouldClose().
void request_quit();
bool quit_requested();

}  // namespace nockvm::app
