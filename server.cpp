#include "fs_lib.hpp"
#include "signals.hpp"
#include "socket.hpp"
#include "transfer.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

constexpr std::uint64_t DEF_CHUNK = 64 * 1024;
constexpr std::uint64_t MIN_CHUNK = 1024;
constexpr std::uint64_t MAX_CHUNK = 4 * 1024 * 1024;

namespace {

net::socket_t g_listen_socket = net::invalid_socket;
std::mutex g_clients_mutex;
std::vector<net::socket_t> g_clients;

void add_client(net::socket_t fd) {
  std::lock_guard<std::mutex> lock(g_clients_mutex);
  g_clients.push_back(fd);
}

void remove_client(net::socket_t fd) {
  std::lock_guard<std::mutex> lock(g_clients_mutex);
  const auto it = std::find(g_clients.begin(), g_clients.end(), fd);
  if (it != g_clients.end())
    g_clients.erase(it);
}

void stop_handler(int) {
  g_stop = 1;
  // Do not call closesocket() from the signal handler.
  // The main thread uses a short accept timeout and closes sockets safely.
}

bool parse_uint64(const std::string &s, std::uint64_t &value) {
  try {
    if (s.empty()) return false;
    std::size_t pos = 0;
    const auto v = std::stoull(s, &pos);
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

} // namespace

// The requested path is allowed only if its normalized location
// is inside the configured root directory.
bool is_safe_path(const fs::path &root, const fs::path &requested) {
  std::error_code ec;

  const fs::path normalized_root =
      fs::weakly_canonical(root, ec);
  if (ec) return false;

  fs::path candidate = requested.is_absolute()
      ? requested
      : normalized_root / requested;

  const fs::path normalized_candidate =
      fs::weakly_canonical(candidate, ec);
  if (ec) return false;

  auto root_it = normalized_root.begin();
  auto root_end = normalized_root.end();
  auto candidate_it = normalized_candidate.begin();
  auto candidate_end = normalized_candidate.end();

  for (; root_it != root_end; ++root_it, ++candidate_it) {
    if (candidate_it == candidate_end ||
        *root_it != *candidate_it)
      return false;
  }

  return true;
}

void handle_client(net::socket_t fd, const fs::path &root_dir) {
  add_client(fd);

  struct SocketGuard {
    net::socket_t fd;
    ~SocketGuard() {
      remove_client(fd);
      net::close_socket(fd);
    }
  } guard{fd};

  try {
    std::string request;
    if (!net::recv_line(fd, request))
      return;

    std::istringstream input(request);
    std::string command;
    std::string path_string;
    std::uint64_t chunk = DEF_CHUNK;
    std::uint64_t size = 0;

    input >> command >> path_string;

    if (command == "GET") {
      std::string chunk_string;
      if (input >> chunk_string) {
        if (!parse_uint64(chunk_string, chunk)) {
          const std::string error = "ERR invalid request\n";
          net::send_all(fd, error.data(), error.size());
          return;
        }
      }
    } else if (command == "PUT") {
      std::string size_string;
      std::string chunk_string;

      if (!(input >> size_string)) {
        const std::string error = "ERR invalid file size\n";
        net::send_all(fd, error.data(), error.size());
        return;
      }

      if (!parse_uint64(size_string, size)) {
        const std::string error = "ERR invalid file size\n";
        net::send_all(fd, error.data(), error.size());
        return;
      }

      if (!(input >> chunk_string) ||
          !parse_uint64(chunk_string, chunk)) {
        const std::string error = "ERR invalid request\n";
        net::send_all(fd, error.data(), error.size());
        return;
      }
    }

    if (command.empty() || path_string.empty() ||
        chunk < MIN_CHUNK || chunk > MAX_CHUNK ||
        !is_safe_path(root_dir, fs::path(path_string))) {
      const std::string error = "ERR invalid request\n";
      net::send_all(fd, error.data(), error.size());
      return;
    }

    fs::path full_path = fs::path(path_string);
    if (!full_path.is_absolute())
      full_path = root_dir / full_path;

    if (command == "GET") {
      if (!fs_is_regular_file(full_path.string().c_str())) {
        const std::string error = "ERR file not found\n";
        net::send_all(fd, error.data(), error.size());
        return;
      }

      const std::int64_t file_size =
          fs_file_size(full_path.string().c_str());

      if (file_size < 0) {
        const std::string error = "ERR file not found\n";
        net::send_all(fd, error.data(), error.size());
        return;
      }

      const std::string response =
          "OK " + std::to_string(file_size) + "\n";

      if (net::send_all(fd, response.data(), response.size()))
        transfer::send_file(fd, full_path, chunk, nullptr);

    } else if (command == "PUT") {
      // Create parent directories only if they already exist;
      // the server does not create arbitrary directory trees.
      const fs::path parent = full_path.parent_path();
      if (!parent.empty() && !fs::exists(parent)) {
        const std::string error = "ERR invalid path\n";
        net::send_all(fd, error.data(), error.size());
        return;
      }

      const std::string response = "OK\n";
      if (net::send_all(fd, response.data(), response.size()))
        transfer::receive_file(fd, full_path, size, chunk, nullptr);

    } else {
      const std::string error = "ERR bad command\n";
      net::send_all(fd, error.data(), error.size());
    }

  } catch (const std::exception &e) {
    std::cerr << "Client error: " << e.what() << "\n";
  }
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

  #ifdef _WIN32
  std::signal(SIGINT, stop_handler);
  std::signal(SIGBREAK, stop_handler);
#else
  std::signal(SIGINT, stop_handler);
#endif

  std::uint16_t port = 4040;
  fs::path root_dir = ".";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "-p" && i + 1 < argc) {
      if (!parse_port(argv[++i], port)) {
        std::cerr << "Invalid port\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
      }
    } else if (arg == "-root" && i + 1 < argc) {
      root_dir = argv[++i];
    } else {
      std::cerr << "Unknown or incomplete argument: " << arg << "\n";
#ifdef _WIN32
      WSACleanup();
#endif
      return 1;
    }
  }

  std::error_code ec;
  if (!fs::exists(root_dir, ec) ||
      !fs::is_directory(root_dir, ec)) {
    std::cerr << "Invalid root directory\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  root_dir = fs::weakly_canonical(root_dir, ec);
  if (ec) {
    std::cerr << "Cannot resolve root directory\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  g_listen_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (g_listen_socket == net::invalid_socket) {
    std::cerr << "socket() failed\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  BOOL reuse = TRUE;
  ::setsockopt(g_listen_socket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&reuse), sizeof(reuse));

#ifdef _WIN32
  // Non-blocking accept lets the main loop notice Ctrl+C immediately.
  u_long non_blocking = 1;
  if (ioctlsocket(g_listen_socket, FIONBIO, &non_blocking) != 0) {
    std::cerr << "ioctlsocket() failed\\n";
    net::close_socket(g_listen_socket);
    g_listen_socket = net::invalid_socket;
    WSACleanup();
    return 1;
  }
#endif

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);

  if (::bind(g_listen_socket,
             reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) == SOCKET_ERROR) {
    std::cerr << "bind() failed\n";
    net::close_socket(g_listen_socket);
    g_listen_socket = net::invalid_socket;
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  if (::listen(g_listen_socket, 16) == SOCKET_ERROR) {
    std::cerr << "listen() failed\n";
    net::close_socket(g_listen_socket);
    g_listen_socket = net::invalid_socket;
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  std::cout << "Server listening on port " << port
            << " (root: " << root_dir.string() << ")\n";

  while (!g_stop) {
    sockaddr_in client_address{};
#ifdef _WIN32
    int address_size = sizeof(client_address);
#else
    socklen_t address_size = sizeof(client_address);
#endif

    const net::socket_t client =
        ::accept(g_listen_socket,
                 reinterpret_cast<sockaddr *>(&client_address),
                 &address_size);

    if (client == net::invalid_socket) {
      if (g_stop) break;
#ifdef _WIN32
      const int error = WSAGetLastError();
      if (error == WSAETIMEDOUT ||
          error == WSAEWOULDBLOCK)
        continue;
#endif
      continue;
    }

    if (g_stop) {
      net::close_socket(client);
      break;
    }

    std::thread(handle_client, client, root_dir).detach();
  }

  g_stop = 1;

  if (g_listen_socket != net::invalid_socket) {
    net::close_socket(g_listen_socket);
    g_listen_socket = net::invalid_socket;
  }

  // Unblock active transfers.
  std::vector<net::socket_t> clients;
  {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    clients = g_clients;
  }

  for (const auto fd : clients) {
    ::shutdown(fd, SD_BOTH);
  }

  for (int i = 0; i < 100; ++i) {
    bool empty = false;
    {
      std::lock_guard<std::mutex> lock(g_clients_mutex);
      empty = g_clients.empty();
    }
    if (empty) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

#ifdef _WIN32
  WSACleanup();
#endif
  return 0;
}
