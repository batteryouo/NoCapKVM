#include "nockvm/discovery/identity.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include "config_dir.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace nockvm::discovery {
namespace {

uint64_t generate_device_id() {
  std::random_device rd;
  std::uniform_int_distribution<uint64_t> dist;
  uint64_t high = dist(rd);
  uint64_t low = dist(rd);
  return (high << 32) ^ low;
}

}  // namespace

uint64_t get_or_create_device_id() {
  const std::filesystem::path dir = config_dir();
  const std::filesystem::path id_file = dir / "device_id";

  std::ifstream in(id_file);
  if (in) {
    uint64_t id = 0;
    in >> std::hex >> id;
    if (in) return id;
  }

  const uint64_t id = generate_device_id();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream out(id_file);
  out << std::hex << id;
  return id;
}

std::string get_hostname() {
#ifdef _WIN32
  char buf[256];
  DWORD size = sizeof(buf);
  if (GetComputerNameA(buf, &size)) return std::string(buf, size);
  return "unknown-host";
#else
  char buf[256];
  if (gethostname(buf, sizeof(buf)) == 0) return std::string(buf);
  return "unknown-host";
#endif
}

}  // namespace nockvm::discovery
