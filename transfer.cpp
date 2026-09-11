#include "transfer.hpp"

#include "fs_lib.hpp"
#include "signals.hpp"
#include "socket.hpp"

#include <algorithm>
#include <fstream>
#include <vector>

namespace transfer {

bool send_file(net::socket_t fd,
               const std::filesystem::path &source,
               std::uint64_t chunk,
               const ProgressCallback &cb) {
  std::ifstream file(source, std::ios::binary);
  if (!file) return false;

  const std::int64_t signed_size = fs_file_size(source.string().c_str());
  if (signed_size < 0) return false;

  const std::uint64_t total = static_cast<std::uint64_t>(signed_size);
  std::vector<char> buffer(static_cast<std::size_t>(chunk));
  std::uint64_t done = 0;

  while (done < total && !g_stop) {
    const std::uint64_t want =
        std::min<std::uint64_t>(buffer.size(), total - done);

    file.read(buffer.data(), static_cast<std::streamsize>(want));
    const std::streamsize n = file.gcount();

    if (n <= 0) return false;

    if (!net::send_all(fd, buffer.data(), static_cast<std::size_t>(n)))
      return false;

    done += static_cast<std::uint64_t>(n);
    if (cb) cb(done, total);
  }

  return done == total && !g_stop;
}

bool receive_file(net::socket_t fd,
                  const std::filesystem::path &dest,
                  std::uint64_t total,
                  std::uint64_t chunk,
                  const ProgressCallback &cb) {
  const auto tmp = std::filesystem::path(dest.string() + ".part");

  std::ofstream file(tmp, std::ios::binary);
  if (!file) return false;

  std::vector<char> buffer(static_cast<std::size_t>(chunk));
  std::uint64_t received = 0;

  while (received < total && !g_stop) {
    const std::uint64_t want =
        std::min<std::uint64_t>(buffer.size(), total - received);

#ifdef _WIN32
    const int n = ::recv(fd, buffer.data(), static_cast<int>(want), 0);
    if (n == SOCKET_ERROR) {
      const int err = WSAGetLastError();
      if (err == WSAEINTR || err == WSAETIMEDOUT) continue;
      file.close();
      fs_remove(tmp.string().c_str());
      return false;
    }
#else
    const ssize_t n = ::recv(fd, buffer.data(), want, 0);
    if (n < 0) continue;
#endif

    if (n <= 0) {
      file.close();
      fs_remove(tmp.string().c_str());
      return false;
    }

    file.write(buffer.data(), static_cast<std::streamsize>(n));
    if (!file) {
      file.close();
      fs_remove(tmp.string().c_str());
      return false;
    }

    received += static_cast<std::uint64_t>(n);
    if (cb) cb(received, total);
  }

  file.close();

  if (g_stop || received != total) {
    fs_remove(tmp.string().c_str());
    return false;
  }

  if (!fs_rename_safe(tmp.string().c_str(), dest.string().c_str())) {
    fs_remove(tmp.string().c_str());
    return false;
  }

  return true;
}

} // namespace transfer
