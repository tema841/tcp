#include "fs_lib.hpp"
#include "progress.hpp"
#include "signals.hpp"
#include "socket.hpp"
#include "transfer.hpp"

#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace fs = std::filesystem;

constexpr std::uint64_t DEF_CHUNK = 64 * 1024;
constexpr std::uint64_t MIN_CHUNK = 1024;
constexpr std::uint64_t MAX_CHUNK = 4 * 1024 * 1024;

struct Remote {
  std::string host;
  std::string path;
};

bool parse_uint64(const std::string &s, std::uint64_t &value) {
  try {
    if (s.empty()) return false;
    std::size_t pos = 0;
    const unsigned long long v = std::stoull(s, &pos);
    if (pos != s.size()) return false;
    value = static_cast<std::uint64_t>(v);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_port(const std::string &s, std::uint16_t &port) {
  std::uint64_t value = 0;
  if (!parse_uint64(s, value) || value < 1 || value > 65535)
    return false;
  port = static_cast<std::uint16_t>(value);
  return true;
}

bool parse_remote(const std::string &s, Remote &remote) {
  const auto pos = s.find(':');
  if (pos == std::string::npos || pos == 0 || pos + 1 >= s.size())
    return false;

  remote.host = s.substr(0, pos);
  remote.path = s.substr(pos + 1);
  return true;
}

net::socket_t connect_to(const std::string &host, std::uint16_t port) {
  net::socket_t fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd == net::invalid_socket)
    return net::invalid_socket;

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);

  if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) <= 0) {
    net::close_socket(fd);
    return net::invalid_socket;
  }

  if (::connect(fd, reinterpret_cast<sockaddr *>(&address),
                sizeof(address)) == SOCKET_ERROR) {
    net::close_socket(fd);
    return net::invalid_socket;
  }

  // Timeout allows Ctrl+C to be noticed even if the peer stops sending.
  net::set_io_timeout(fd, 1000);
  return fd;
}

void print_usage() {
  std::cout
      << "Usage:\n"
      << "  GET: client -p <port> -chunk <bytes> "
         "-src <IP>:<remote_file> -dst <local_file>\n"
      << "  PUT: client -p <port> -chunk <bytes> "
         "-src <local_file> -dst <IP>:<remote_file>\n";
}

#ifdef _WIN32
bool init_winsock() {
  WSADATA data{};
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}
#endif

int main(int argc, char *argv[]) {
#ifdef _WIN32
  if (!init_winsock()) {
    std::cerr << "WSAStartup failed\n";
    return 1;
  }
#endif

  std::signal(SIGINT, [](int) { g_stop = 1; });

  std::string src;
  std::string dst;
  std::uint16_t port = 4040;
  std::uint64_t chunk = DEF_CHUNK;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "-src" && i + 1 < argc) {
      src = argv[++i];
    } else if (arg == "-dst" && i + 1 < argc) {
      dst = argv[++i];
    } else if (arg == "-p" && i + 1 < argc) {
      if (!parse_port(argv[++i], port)) {
        std::cerr << "Invalid port\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
      }
    } else if (arg == "-chunk" && i + 1 < argc) {
      if (!parse_uint64(argv[++i], chunk)) {
        std::cerr << "Invalid chunk size\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
      }
    } else {
      print_usage();
#ifdef _WIN32
      WSACleanup();
#endif
      return 1;
    }
  }

  if (src.empty() || dst.empty() ||
      chunk < MIN_CHUNK || chunk > MAX_CHUNK) {
    print_usage();
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  Remote remote_src;
  Remote remote_dst;
  const bool src_is_remote = parse_remote(src, remote_src);
  const bool dst_is_remote = parse_remote(dst, remote_dst);

  if (src_is_remote == dst_is_remote) {
    std::cerr << "Exactly one of -src/-dst must be remote\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  // GET: remote source -> local destination.
  if (src_is_remote) {
    const auto fd = connect_to(remote_src.host, port);
    if (fd == net::invalid_socket) {
      std::cerr << "Error: cannot connect to "
                << remote_src.host << ":" << port << "\n";
#ifdef _WIN32
      WSACleanup();
#endif
      return 1;
    }

    const std::string request =
        "GET " + remote_src.path + " " + std::to_string(chunk) + "\n";

    if (!net::send_all(fd, request.data(), request.size())) {
      net::close_socket(fd);
#ifdef _WIN32
      WSACleanup();
#endif
      return 1;
    }

    std::string response;
    if (!net::recv_line(fd, response) ||
        response.rfind("OK ", 0) != 0) {
      std::cerr << (response.empty() ? "Server error" : response) << "\n";
      net::close_socket(fd);
#ifdef _WIN32
      WSACleanup();
#endif
      return 1;
    }

    std::uint64_t total = 0;
    if (!parse_uint64(response.substr(3), total)) {
      std::cerr << "Invalid server response\n";
      net::close_socket(fd);
#ifdef _WIN32
      WSACleanup();
#endif
      return 1;
    }

    const bool ok = transfer::receive_file(
        fd, dst, total, chunk,
        [](std::uint64_t done, std::uint64_t total_bytes) {
          print_progress(done, total_bytes);
        });

    std::cout << "\n";
    net::close_socket(fd);

#ifdef _WIN32
    WSACleanup();
#endif
    return ok ? 0 : 1;
  }

  // PUT: local source -> remote destination.
  if (!fs::is_regular_file(src)) {
    std::cerr << "Source file does not exist or is not a regular file\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  const auto total = fs::file_size(src);
  const auto fd = connect_to(remote_dst.host, port);

  if (fd == net::invalid_socket) {
    std::cerr << "Error: cannot connect to "
              << remote_dst.host << ":" << port << "\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  const std::string request =
      "PUT " + remote_dst.path + " " +
      std::to_string(total) + " " +
      std::to_string(chunk) + "\n";

  if (!net::send_all(fd, request.data(), request.size())) {
    net::close_socket(fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  std::string response;
  if (!net::recv_line(fd, response) || response != "OK") {
    std::cerr << (response.empty() ? "Server error" : response) << "\n";
    net::close_socket(fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  const bool ok = transfer::send_file(
      fd, src, chunk,
      [](std::uint64_t done, std::uint64_t total_bytes) {
        print_progress(done, total_bytes);
      });

  std::cout << "\n";
  net::close_socket(fd);

#ifdef _WIN32
  WSACleanup();
#endif
  return ok ? 0 : 1;
}
