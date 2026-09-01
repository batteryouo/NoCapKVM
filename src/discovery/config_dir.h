#pragma once
#include <cstdlib>
#include <filesystem>

namespace nockvm::discovery {

// Resolves the local config directory, honoring the NOCKVM_HOME override
// (also used to give independent identities to two local instances for
// testing).
inline std::filesystem::path config_dir() {
  if (const char* override_dir = std::getenv("NOCKVM_HOME")) return std::filesystem::path(override_dir);
#ifdef _WIN32
  if (const char* appdata = std::getenv("APPDATA")) return std::filesystem::path(appdata) / "NoCapKVM";
  return std::filesystem::path(".") / "NoCapKVM";
#else
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) return std::filesystem::path(xdg) / "nockvm";
  if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".config" / "nockvm";
  return std::filesystem::path(".") / ".nockvm";
#endif
}

}  // namespace nockvm::discovery
