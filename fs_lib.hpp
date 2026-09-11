#pragma once

#include <cstdint>

extern "C" {
bool fs_is_regular_file(const char *path);
std::int64_t fs_file_size(const char *path);
bool fs_rename_safe(const char *from, const char *to);
bool fs_remove(const char *path);
}
