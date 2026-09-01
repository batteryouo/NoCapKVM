#include "nockvm/discovery/static_keys.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "config_dir.h"

namespace nockvm::discovery {
namespace {

std::string to_hex(const Key32& key) {
  std::ostringstream oss;
  for (uint8_t byte : key) oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
  return oss.str();
}

bool from_hex(const std::string& hex, Key32& out) {
  if (hex.size() != out.size() * 2) return false;
  for (size_t i = 0; i < out.size(); ++i) {
    unsigned int byte = 0;
    std::istringstream iss(hex.substr(i * 2, 2));
    if (!(iss >> std::hex >> byte)) return false;
    out[i] = static_cast<uint8_t>(byte);
  }
  return true;
}

}  // namespace

Keypair get_or_create_static_keypair() {
  const std::filesystem::path file = config_dir() / "identity_keys";

  std::ifstream in(file);
  if (in) {
    std::string pub_hex, priv_hex;
    std::getline(in, pub_hex);
    std::getline(in, priv_hex);
    Keypair kp;
    if (from_hex(pub_hex, kp.public_key) && from_hex(priv_hex, kp.private_key)) return kp;
  }

  const Keypair kp = generate_keypair();
  std::error_code ec;
  std::filesystem::create_directories(config_dir(), ec);
  std::ofstream out(file);
  out << to_hex(kp.public_key) << "\n" << to_hex(kp.private_key) << "\n";
  return kp;
}

}  // namespace nockvm::discovery
