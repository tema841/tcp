#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#ifdef _WIN32
#include <winsock2.h>
namespace net { using socket_t = SOCKET; constexpr socket_t invalid_socket = INVALID_SOCKET; }
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
namespace net { using socket_t = int; constexpr socket_t invalid_socket = -1; }
#endif
namespace net {
bool init(); void cleanup();
bool send_all(socket_t fd,const void* data,std::size_t size);
bool recv_all(socket_t fd,void* data,std::size_t size);
bool recv_line(socket_t fd,std::string& line,std::size_t max_len=65536);
bool set_io_timeout(socket_t fd,int milliseconds);
void close_socket(socket_t fd);
void shutdown_socket(socket_t fd);
std::string peer_address(socket_t fd);
}
