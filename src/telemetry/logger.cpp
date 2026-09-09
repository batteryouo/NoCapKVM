#include "nockvm/telemetry/logger.h"
#include <system_error>

namespace nockvm::telemetry {

RotatingLogger::RotatingLogger(std::filesystem::path dir, std::string base_name, size_t max_bytes, int max_files)
    : dir_(std::move(dir)), base_name_(std::move(base_name)), max_bytes_(max_bytes),
      max_files_(max_files < 1 ? 1 : max_files) {}

std::filesystem::path RotatingLogger::backup_path(int index) const {
  return dir_ / (base_name_ + "." + std::to_string(index));
}

std::filesystem::path RotatingLogger::active_path() const { return dir_ / base_name_; }

bool RotatingLogger::open() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  if (ec) return false;

  const auto path = active_path();
  stream_.open(path, std::ios::app | std::ios::binary);
  if (!stream_.is_open()) return false;

  current_size_ = std::filesystem::exists(path, ec) ? std::filesystem::file_size(path, ec) : 0;
  return true;
}

// Oldest backup is dropped, every remaining backup shifts up by one index,
// and the active file becomes the newest backup -- classic logrotate
// shape. Called with mutex_ already held.
void RotatingLogger::rotate() {
  stream_.close();

  std::error_code ec;
  if (max_files_ > 1) {
    std::filesystem::remove(backup_path(max_files_ - 1), ec);
    for (int i = max_files_ - 2; i >= 1; --i) std::filesystem::rename(backup_path(i), backup_path(i + 1), ec);
    std::filesystem::rename(active_path(), backup_path(1), ec);
  }

  stream_.open(active_path(), std::ios::out | std::ios::trunc | std::ios::binary);
  current_size_ = 0;
  ++rotation_count_;
}

void RotatingLogger::write_line(const std::string& line) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stream_.is_open()) return;

  const size_t needed = line.size() + 1;
  if (current_size_ > 0 && current_size_ + needed > max_bytes_) rotate();

  stream_ << line << '\n';
  stream_.flush();
  current_size_ += needed;
}

int RotatingLogger::rotation_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return rotation_count_;
}

}  // namespace nockvm::telemetry
