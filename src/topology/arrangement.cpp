#include "nockvm/topology/arrangement.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "config_dir.h"

namespace nockvm::topology {
namespace {

std::filesystem::path store_path() { return config_dir() / "screen_arrangement"; }

char direction_char(Direction d) {
  switch (d) {
    case Direction::Left: return 'L';
    case Direction::Right: return 'R';
    case Direction::Up: return 'U';
    case Direction::Down: return 'D';
  }
  return 'R';
}

bool direction_from_char(char c, Direction& out) {
  switch (c) {
    case 'L': out = Direction::Left; return true;
    case 'R': out = Direction::Right; return true;
    case 'U': out = Direction::Up; return true;
    case 'D': out = Direction::Down; return true;
    default: return false;
  }
}

}  // namespace

ScreenArrangement::ScreenArrangement() {
  std::ifstream in(store_path());
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream iss(line);
    uint64_t device_id = 0;
    std::string dir_str;
    int32_t offset = 0;
    if (!(iss >> std::hex >> device_id >> std::dec >> dir_str >> offset)) continue;
    if (dir_str.size() != 1) continue;
    Direction direction;
    if (!direction_from_char(dir_str[0], direction)) continue;
    entries_[device_id] = ArrangementEntry{device_id, direction, offset};
  }
}

std::optional<ArrangementEntry> ScreenArrangement::get(uint64_t device_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = entries_.find(device_id);
  if (it == entries_.end()) return std::nullopt;
  return it->second;
}

void ScreenArrangement::set(uint64_t device_id, Direction direction, int32_t offset) {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_[device_id] = ArrangementEntry{device_id, direction, offset};
  save();
}

void ScreenArrangement::forget(uint64_t device_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.erase(device_id);
  save();
}

std::vector<ArrangementEntry> ScreenArrangement::list() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ArrangementEntry> result;
  result.reserve(entries_.size());
  for (const auto& [device_id, entry] : entries_) result.push_back(entry);
  return result;
}

void ScreenArrangement::save() const {
  std::error_code ec;
  std::filesystem::create_directories(config_dir(), ec);
  std::ofstream out(store_path());
  for (const auto& [device_id, entry] : entries_) {
    out << std::hex << device_id << " " << direction_char(entry.direction) << " " << std::dec << entry.offset
        << "\n";
  }
}

}  // namespace nockvm::topology
