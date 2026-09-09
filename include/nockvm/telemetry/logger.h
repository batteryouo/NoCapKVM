#pragma once
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace nockvm::telemetry {

// A single append-only log file bounded to max_bytes, kept alongside up to
// (max_files - 1) rotated backups (dir/base_name.1 the most recent backup,
// dir/base_name.(max_files-1) the oldest, dropped on the next rotation).
// Pure file management -- no knowledge of what's being logged -- so it's
// usable standalone in tests against a scratch directory.
class RotatingLogger {
public:
  RotatingLogger(std::filesystem::path dir, std::string base_name, size_t max_bytes, int max_files);

  // Creates dir if needed and opens (resuming, not truncating) the active
  // file. Returns false on failure; write_line() is then a silent no-op.
  bool open();

  // Appends one line (plus a trailing newline), rotating first if the
  // write would push the active file past max_bytes. No-op if open()
  // wasn't called or failed. A single line longer than max_bytes on its
  // own is still written whole (rotation can't split a line); the next
  // write rotates again immediately.
  void write_line(const std::string& line);

  std::filesystem::path active_path() const;

  // How many rotations have happened since open(). Lets a caller notice a
  // rotation right after it happens (e.g. to log a follow-up event) without
  // this class needing to know how to format one itself.
  int rotation_count() const;

private:
  void rotate();
  std::filesystem::path backup_path(int index) const;

  std::filesystem::path dir_;
  std::string base_name_;
  size_t max_bytes_;
  int max_files_;
  mutable std::mutex mutex_;
  std::ofstream stream_;
  size_t current_size_ = 0;
  int rotation_count_ = 0;
};

}  // namespace nockvm::telemetry
