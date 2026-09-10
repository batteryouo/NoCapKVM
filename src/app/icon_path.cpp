#ifndef _WIN32

#include "icon_path.h"

#include <array>
#include <filesystem>

#include <unistd.h>

namespace nockvm::app {
namespace {

constexpr const char* kIconFileName = "NoCapKVM.ico";
#ifndef NOCKVM_INSTALLED_ICON_PATH
#define NOCKVM_INSTALLED_ICON_PATH "/usr/share/nockvm/NoCapKVM.ico"
#endif

std::filesystem::path executable_icon_path() {
  std::array<char, 4096> buffer{};
  const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (length <= 0) return {};
  return std::filesystem::path(buffer.data()).parent_path() / kIconFileName;
}

}  // namespace

std::string app_icon_path() {
  const std::array<std::filesystem::path, 3> candidates = {
      executable_icon_path(), NOCKVM_INSTALLED_ICON_PATH, std::filesystem::path("res/icons") / kIconFileName};
  for (const auto& path : candidates) {
    if (!path.empty() && std::filesystem::is_regular_file(path)) return path.string();
  }
  return {};
}

}  // namespace nockvm::app

#endif
