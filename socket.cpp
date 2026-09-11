#include "socket.hpp"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace net {

bool send_all(socket_t fd, const void *data, std::size_t size) {
  const char *p = static_cast<const char *>(data);
  std::size_t sent = 0;

  while (sent < size) {
#ifdef _WIN32
    const int part = static_cast<int>(
        std::min<std::size_t>(size - sent, 64 * 1024));
    const int n = ::send(fd, p + sent, part, 0);
    if (n == SOCKET_ERROR) {
      const int err = WSAGetLastError();
      if (err == WSAEINTR) continue;
      if (err == WSAETIMEDOUT) continue;
      return false;
    }
#else
    const ssize_t n = ::send(fd, p + sent, size - sent, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
#endif
    if (n <= 0) return false;
    sent += static_cast<std::size_t>(n);
  }

  return true;
}

bool recv_all(socket_t fd, void *data, std::size_t size) {
  char *p = static_cast<char *>(data);
  std::size_t received = 0;

  while (received < size) {
#ifdef _WIN32
    const int part = static_cast<int>(
        std::min<std::size_t>(size - received, 64 * 1024));
    const int n = ::recv(fd, p + received, part, 0);
    if (n == SOCKET_ERROR) {
      const int err = WSAGetLastError();
      if (err == WSAEINTR) continue;
      if (err == WSAETIMEDOUT) continue;
      return false;
    }
#else
    const ssize_t n = ::recv(fd, p + received, size - received, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
#endif
    if (n <= 0) return false;
    received += static_cast<std::size_t>(n);
  }

  return true;
}

bool recv_line(socket_t fd, std::string &line) {
  line.clear();
  char c = '\0';

  while (true) {
#ifdef _WIN32
    const int n = ::recv(fd, &c, 1, 0);
    if (n == SOCKET_ERROR) {
      const int err = WSAGetLastError();
      if (err == WSAEINTR || err == WSAETIMEDOUT) continue;
      return false;
    }
#else
    const ssize_t n = ::recv(fd, &c, 1, 0);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
#endif
    if (n <= 0) return false;

    if (c == '\n') return true;
    if (c != '\r') line.push_back(c);

    if (line.size() > 64 * 1024) return false;
  }
}

bool set_io_timeout(socket_t fd, int milliseconds) {
#ifdef _WIN32
  const DWORD value = milliseconds < 0 ? 0 : static_cast<DWORD>(milliseconds);
  return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                    reinterpret_cast<const char *>(&value),
                    sizeof(value)) == 0 &&
         setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                    reinterpret_cast<const char *>(&value),
                    sizeof(value)) == 0;
#else
  timeval tv{};
  tv.tv_sec = milliseconds / 1000;
  tv.tv_usec = (milliseconds % 1000) * 1000;
  return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
         setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

void close_socket(socket_t fd) {
  if (fd == invalid_socket) return;
#ifdef _WIN32
  ::closesocket(fd);
#else
  ::close(fd);
#endif
}

} // namespace net
