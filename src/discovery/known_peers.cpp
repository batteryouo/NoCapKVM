#include "nockvm/discovery/known_peers.h"
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

std::filesystem::path store_path() { return config_dir() / "known_peers"; }

}  // namespace

KnownPeers::KnownPeers() {
  std::ifstream in(store_path());
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream iss(line);
    uint64_t device_id = 0;
    std::string pub_hex;
    if (!(iss >> std::hex >> device_id >> pub_hex)) continue;
    Key32 pubkey;
    if (from_hex(pub_hex, pubkey)) peers_[device_id] = pubkey;
  }
}

bool KnownPeers::is_known(uint64_t device_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return peers_.count(device_id) != 0;
}

std::optional<Key32> KnownPeers::get_pubkey(uint64_t device_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = peers_.find(device_id);
  if (it == peers_.end()) return std::nullopt;
  return it->second;
}

void KnownPeers::remember(uint64_t device_id, const Key32& pubkey) {
  std::lock_guard<std::mutex> lock(mutex_);
  peers_[device_id] = pubkey;
  save();
}

void KnownPeers::save() const {
  std::error_code ec;
  std::filesystem::create_directories(config_dir(), ec);
  std::ofstream out(store_path());
  for (const auto& [device_id, pubkey] : peers_) out << std::hex << device_id << " " << to_hex(pubkey) << "\n";
}

}  // namespace nockvm::discovery
