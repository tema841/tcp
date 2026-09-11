#pragma once

#include "socket.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>

namespace transfer {

using ProgressCallback =
    std::function<void(std::uint64_t, std::uint64_t)>;

bool send_file(net::socket_t fd,
               const std::filesystem::path &source,
               std::uint64_t chunk,
               const ProgressCallback &cb);

bool receive_file(net::socket_t fd,
                  const std::filesystem::path &dest,
                  std::uint64_t total,
                  std::uint64_t chunk,
                  const ProgressCallback &cb);

} // namespace transfer
