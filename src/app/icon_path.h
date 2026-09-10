#pragma once

#include <string>

namespace nockvm::app {

// Returns the installed icon when available, with paths useful to an
// uninstalled build as fallbacks.
std::string app_icon_path();

}  // namespace nockvm::app
