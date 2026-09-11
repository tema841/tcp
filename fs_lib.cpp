#include "fs_lib.hpp"

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

extern "C" {

bool fs_is_regular_file(const char *path) {
  if (!path) return false;
  std::error_code ec;
  return fs::is_regular_file(fs::path(path), ec);
}

std::int64_t fs_file_size(const char *path) {
  if (!path) return -1;
  std::error_code ec;
  auto sz = fs::file_size(fs::path(path), ec);
  return ec ? -1 : static_cast<std::int64_t>(sz);
}

bool fs_rename_safe(const char *from, const char *to) {
  if (!from || !to) return false;
  std::error_code ec;
  fs::rename(fs::path(from), fs::path(to), ec);
  return !ec;
}

bool fs_remove(const char *path) {
  if (!path) return false;
  std::error_code ec;
  return fs::remove(fs::path(path), ec);
}

} // extern "C"
