#include "signals.hpp"
#include "socket.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

constexpr std::uint16_t TEST_PORT = 5050;

struct TestResult {
  bool passed;
  std::string message;
};

#ifdef _WIN32
static PROCESS_INFORMATION g_server_process{};
#else
static int g_server_pid = -1;
#endif

static fs::path g_root;

#ifdef _WIN32
static std::string quote_arg(const std::string &s) {
  std::string result = "\"";
  for (char c : s) {
    if (c == '"') result += "\\\"";
    else result += c;
  }
  result += "\"";
  return result;
}

static fs::path executable_dir() {
  char buffer[MAX_PATH]{};
  const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
  if (len == 0 || len == MAX_PATH)
    return fs::current_path();
  return fs::path(std::string(buffer, len)).parent_path();
}
#endif

static void write_file(const fs::path &path, const std::string &data) {
  std::ofstream file(path, std::ios::binary);
  file.write(data.data(), static_cast<std::streamsize>(data.size()));
}

static std::string read_file(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

static bool connect_socket(net::socket_t &fd, int timeout_ms = 1000) {
  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(timeout_ms);

  while (std::chrono::steady_clock::now() < deadline) {
    fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (fd != net::invalid_socket) {
      sockaddr_in address{};
      address.sin_family = AF_INET;
      address.sin_port = htons(TEST_PORT);
      inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

      if (::connect(fd,
                    reinterpret_cast<sockaddr *>(&address),
                    sizeof(address)) == 0) {
        return true;
      }

      net::close_socket(fd);
      fd = net::invalid_socket;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  return false;
}

static bool send_request(const std::string &request,
                         std::string &response,
                         std::string *body = nullptr,
                         std::size_t body_size = 0) {
  net::socket_t fd = net::invalid_socket;

  if (!connect_socket(fd))
    return false;

  if (!net::send_all(fd, request.data(), request.size()) ||
      !net::recv_line(fd, response)) {
    net::close_socket(fd);
    return false;
  }

  if (body && body_size > 0) {
    body->resize(body_size);

    if (!net::recv_all(fd, body->data(), body_size)) {
      net::close_socket(fd);
      return false;
    }
  }

  net::close_socket(fd);
  return true;
}

static bool send_put(const std::string &path,
                     const std::string &data,
                     std::uint64_t chunk,
                     std::string &response) {
  net::socket_t fd = net::invalid_socket;

  if (!connect_socket(fd))
    return false;

  const std::string request =
      "PUT " + path + " " +
      std::to_string(data.size()) + " " +
      std::to_string(chunk) + "\n";

  if (!net::send_all(fd, request.data(), request.size()) ||
      !net::recv_line(fd, response)) {
    net::close_socket(fd);
    return false;
  }

  if (response != "OK") {
    net::close_socket(fd);
    return true;
  }

  const bool ok =
      data.empty() ||
      net::send_all(fd, data.data(), data.size());

  net::close_socket(fd);
  return ok;
}

#ifdef _WIN32
static bool run_process(const fs::path &program,
                        const std::vector<std::string> &args) {
  std::string command = quote_arg(program.string());

  for (const auto &arg : args)
    command += " " + quote_arg(arg);

  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);

  PROCESS_INFORMATION process{};

  std::vector<char> command_buffer(command.begin(), command.end());
  command_buffer.push_back('\0');

  if (!CreateProcessA(
          nullptr,
          command_buffer.data(),
          nullptr,
          nullptr,
          FALSE,
          CREATE_NO_WINDOW,
          nullptr,
          program.parent_path().string().c_str(),
          &startup,
          &process)) {
    return false;
  }

  const DWORD result =
      WaitForSingleObject(process.hProcess, INFINITE);

  DWORD exit_code = 1;
  if (result == WAIT_OBJECT_0)
    GetExitCodeProcess(process.hProcess, &exit_code);

  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);

  return exit_code == 0;
}
#else
static bool run_process(const fs::path &program,
                        const std::vector<std::string> &args) {
  std::string command = "\"" + program.string() + "\"";
  for (const auto &arg : args)
    command += " \"" + arg + "\"";
  return std::system(command.c_str()) == 0;
}
#endif

static bool start_server() {
  g_root =
      fs::temp_directory_path() /
      ("tcpip_test_root_" +
#ifdef _WIN32
       std::to_string(GetCurrentProcessId())
#else
       std::to_string(getpid())
#endif
      );

  std::error_code ec;
  fs::remove_all(g_root, ec);
  fs::create_directories(g_root, ec);

  if (ec) return false;

#ifdef _WIN32
  const fs::path server = executable_dir() / "server.exe";
  const std::string command =
      quote_arg(server.string()) +
      " -p 5050 -root " +
      quote_arg(g_root.string());

  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);

  ZeroMemory(&g_server_process, sizeof(g_server_process));

  std::vector<char> buffer(command.begin(), command.end());
  buffer.push_back('\0');

  if (!CreateProcessA(
          nullptr,
          buffer.data(),
          nullptr,
          nullptr,
          FALSE,
          CREATE_NEW_PROCESS_GROUP,
          nullptr,
          server.parent_path().string().c_str(),
          &startup,
          &g_server_process)) {
    return false;
  }
#else
  g_server_pid = fork();

  if (g_server_pid < 0)
    return false;

  if (g_server_pid == 0) {
    execl("./server", "./server",
          "-p", "5050",
          "-root", g_root.c_str(),
          static_cast<char *>(nullptr));
    _exit(127);
  }
#endif

  net::socket_t fd = net::invalid_socket;
  const bool ready = connect_socket(fd, 5000);

  if (fd != net::invalid_socket)
    net::close_socket(fd);

  if (!ready) {
#ifdef _WIN32
    TerminateProcess(g_server_process.hProcess, 1);
    WaitForSingleObject(g_server_process.hProcess, 2000);
    CloseHandle(g_server_process.hThread);
    CloseHandle(g_server_process.hProcess);
    ZeroMemory(&g_server_process, sizeof(g_server_process));
#else
    kill(g_server_pid, SIGKILL);
    waitpid(g_server_pid, nullptr, 0);
    g_server_pid = -1;
#endif
    fs::remove_all(g_root, ec);
    return false;
  }

  return true;
}

static void stop_server() {
#ifdef _WIN32
  if (!g_server_process.hProcess)
    return;

  // CTRL_BREAK_EVENT is used because it is reliably delivered to
  // a Windows process group. The server handles it like SIGINT.
  GenerateConsoleCtrlEvent(
      CTRL_BREAK_EVENT,
      g_server_process.dwProcessId);

  const DWORD result =
      WaitForSingleObject(g_server_process.hProcess, 3000);

  if (result != WAIT_OBJECT_0) {
    TerminateProcess(g_server_process.hProcess, 1);
    WaitForSingleObject(g_server_process.hProcess, 1000);
  }

  CloseHandle(g_server_process.hThread);
  CloseHandle(g_server_process.hProcess);
  ZeroMemory(&g_server_process, sizeof(g_server_process));

#else
  if (g_server_pid <= 0)
    return;

  kill(g_server_pid, SIGINT);

  for (int i = 0; i < 100; ++i) {
    int status = 0;
    const pid_t result =
        waitpid(g_server_pid, &status, WNOHANG);

    if (result == g_server_pid) {
      g_server_pid = -1;
      fs::remove_all(g_root);
      return;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(10));
  }

  kill(g_server_pid, SIGKILL);
  waitpid(g_server_pid, nullptr, 0);
  g_server_pid = -1;
#endif

  fs::remove_all(g_root);
}

static bool wait_for_file(const fs::path &path,
                          int timeout_ms = 1000) {
  const auto deadline =
      std::chrono::steady_clock::now() +
      std::chrono::milliseconds(timeout_ms);

  while (std::chrono::steady_clock::now() < deadline) {
    if (fs::exists(path))
      return true;

    std::this_thread::sleep_for(
        std::chrono::milliseconds(10));
  }

  return fs::exists(path);
}

static TestResult test_server_startup() {
#ifdef _WIN32
  const bool running = g_server_process.hProcess != nullptr;
#else
  const bool running = g_server_pid > 0;
#endif
  return {running, "server started on port 5050"};
}

static TestResult test_get_default_chunk() {
  const fs::path source = g_root / "source.bin";
  const std::string data(12000, 'A');
  write_file(source, data);

  std::string response;
  std::string body;

  const bool ok =
      send_request("GET source.bin\n",
                   response,
                   &body,
                   data.size());

  return {
      ok && response == "OK 12000" && body == data,
      "GET with default chunk works"};
}

static TestResult test_get_small_chunk() {
  const fs::path source = g_root / "small.bin";
  const std::string data =
      "0123456789abcdefghijklmnopqrstuvwxyz";

  write_file(source, data);

  std::string response;
  std::string body;

  const bool ok =
      send_request("GET small.bin 1024\n",
                   response,
                   &body,
                   data.size());

  return {
      ok &&
          response == "OK " + std::to_string(data.size()) &&
          body == data,
      "GET with explicit chunk works"};
}

static TestResult test_get_absolute_path() {
  const fs::path source = g_root / "absolute.bin";
  const std::string data = "absolute-path-test";
  write_file(source, data);

  std::string response;
  std::string body;

  const bool ok =
      send_request("GET " + source.string() + " 1024\n",
                   response,
                   &body,
                   data.size());

  return {
      ok &&
          response == "OK " + std::to_string(data.size()) &&
          body == data,
      "absolute path inside root works"};
}

static TestResult test_get_missing() {
  std::string response;
  const bool ok =
      send_request("GET missing.bin 4096\n", response);

  return {
      ok && response == "ERR file not found",
      "missing GET rejected"};
}

static TestResult test_invalid_command() {
  std::string response;
  const bool ok =
      send_request("DELETE file.bin 4096\n", response);

  return {
      ok && response == "ERR bad command",
      "invalid command rejected"};
}

static TestResult test_invalid_chunk_low() {
  std::string response;
  const bool ok =
      send_request("GET source.bin 512\n", response);

  return {
      ok && response == "ERR invalid request",
      "chunk below minimum rejected"};
}

static TestResult test_invalid_chunk_high() {
  std::string response;
  const bool ok =
      send_request("GET source.bin 5000000\n", response);

  return {
      ok && response == "ERR invalid request",
      "chunk above maximum rejected"};
}

static TestResult test_path_traversal() {
  std::string response;

#ifdef _WIN32
  const std::string outside =
      "..\\..\\Windows\\System32\\drivers\\etc\\hosts";
#else
  const std::string outside = "../../etc/passwd";
#endif

  const bool ok =
      send_request("GET " + outside + " 4096\n", response);

  return {
      ok && response == "ERR invalid request",
      "path traversal rejected"};
}

static TestResult test_invalid_put_size() {
  std::string response;
  const bool ok =
      send_request("PUT broken.bin nope 4096\n", response);

  return {
      ok && response == "ERR invalid file size",
      "invalid PUT size rejected"};
}

static TestResult test_put() {
  const std::string data =
      "PUT test with several chunks and enough bytes "
      "to exercise transfer.";

  std::string response;
  const bool ok =
      send_put("uploaded.bin", data, 1024, response);

  const fs::path destination =
      g_root / "uploaded.bin";

  const bool ready = wait_for_file(destination);

  return {
      ok && response == "OK" &&
          ready && read_file(destination) == data,
      "PUT transfers file correctly"};
}

static TestResult test_zero_byte_put() {
  std::string response;
  const bool ok =
      send_put("empty.bin", "", 1024, response);

  const fs::path destination =
      g_root / "empty.bin";

  const bool ready = wait_for_file(destination);

  return {
      ok && response == "OK" &&
          ready && fs::file_size(destination) == 0,
      "zero-byte PUT creates empty file"};
}

static TestResult test_client_get() {
  const fs::path source =
      g_root / "client_source.bin";

  const fs::path destination =
      fs::temp_directory_path() /
      ("client_get_" +
#ifdef _WIN32
       std::to_string(GetCurrentProcessId())
#else
       std::to_string(getpid())
#endif
       + ".bin");

  const std::string data(5000, 'G');

  write_file(source, data);
  fs::remove(destination);

#ifdef _WIN32
  const fs::path client = executable_dir() / "client.exe";
#else
  const fs::path client = "./client";
#endif

  const bool ok =
      run_process(client, {
          "-p", "5050",
          "-chunk", "1024",
          "-src", "127.0.0.1:" + source.string(),
          "-dst", destination.string()
      });

  const bool same =
      fs::exists(destination) &&
      read_file(destination) == data;

  fs::remove(destination);

  return {
      ok && same,
      "client GET transfers file correctly"};
}

static TestResult test_client_put() {
  const fs::path source =
      fs::temp_directory_path() /
      ("client_put_" +
#ifdef _WIN32
       std::to_string(GetCurrentProcessId())
#else
       std::to_string(getpid())
#endif
       + ".bin");

  const std::string data(7000, 'P');
  const std::string remote = "client_uploaded.bin";
  const fs::path destination = g_root / remote;

  write_file(source, data);
  fs::remove(destination);

#ifdef _WIN32
  const fs::path client = executable_dir() / "client.exe";
#else
  const fs::path client = "./client";
#endif

  const bool ok =
      run_process(client, {
          "-p", "5050",
          "-chunk", "1024",
          "-src", source.string(),
          "-dst", "127.0.0.1:" + remote
      });

  const bool same =
      fs::exists(destination) &&
      read_file(destination) == data;

  fs::remove(source);

  return {
      ok && same,
      "client PUT transfers file correctly"};
}

static TestResult test_client_bad_arguments() {
#ifdef _WIN32
  const fs::path client = executable_dir() / "client.exe";
#else
  const fs::path client = "./client";
#endif

  const bool ok = !run_process(client, {});
  return {ok, "client rejects missing arguments"};
}

static TestResult test_empty_connection() {
  net::socket_t fd = net::invalid_socket;

  if (!connect_socket(fd))
    return {false, "could not connect"};

  net::close_socket(fd);

  std::this_thread::sleep_for(
      std::chrono::milliseconds(50));

  return {true, "empty connection handled"};
}

static TestResult test_stop_with_active_connection() {
  net::socket_t fd = net::invalid_socket;

  if (!connect_socket(fd))
    return {false, "could not connect"};

#ifdef _WIN32
  const bool signal_sent =
      GenerateConsoleCtrlEvent(
          CTRL_BREAK_EVENT,
          g_server_process.dwProcessId) != 0;

  const DWORD result =
      WaitForSingleObject(
          g_server_process.hProcess, 3000);

  const bool stopped = result == WAIT_OBJECT_0;

  net::close_socket(fd);

  if (!stopped)
    TerminateProcess(g_server_process.hProcess, 1);

  if (g_server_process.hThread)
    CloseHandle(g_server_process.hThread);
  if (g_server_process.hProcess)
    CloseHandle(g_server_process.hProcess);

  ZeroMemory(&g_server_process,
             sizeof(g_server_process));

  fs::remove_all(g_root);

  return {
      signal_sent && stopped,
      "server stops cleanly on console control event"};

#else
  kill(g_server_pid, SIGINT);

  bool stopped = false;

  for (int i = 0; i < 100; ++i) {
    int status = 0;

    const pid_t result =
        waitpid(g_server_pid, &status, WNOHANG);

    if (result == g_server_pid) {
      stopped = WIFEXITED(status);
      g_server_pid = -1;
      break;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(10));
  }

  net::close_socket(fd);
  fs::remove_all(g_root);

  return {
      stopped,
      "server stops cleanly on SIGINT"};
#endif
}

int main() {
#ifdef _WIN32
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    std::cerr << "WSAStartup failed\n";
    return 1;
  }
#endif

  std::cout << "=== Running Integration Tests ===\n\n";

  if (!start_server()) {
    std::cerr << "Could not start test server\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
  }

  const std::vector<
      std::pair<std::string, TestResult (*)()>> test_list = {
      {"Server Startup", test_server_startup},
      {"GET Default Chunk", test_get_default_chunk},
      {"GET Explicit Chunk", test_get_small_chunk},
      {"GET Absolute Path", test_get_absolute_path},
      {"GET Missing File", test_get_missing},
      {"Invalid Command", test_invalid_command},
      {"Invalid Small Chunk", test_invalid_chunk_low},
      {"Invalid Large Chunk", test_invalid_chunk_high},
      {"Path Traversal", test_path_traversal},
      {"Invalid PUT Size", test_invalid_put_size},
      {"PUT", test_put},
      {"Zero Byte PUT", test_zero_byte_put},
      {"Client GET", test_client_get},
      {"Client PUT", test_client_put},
      {"Client Bad Arguments", test_client_bad_arguments},
      {"Empty Connection", test_empty_connection},
      {"SIGINT Shutdown", test_stop_with_active_connection}
  };

  int passed = 0;

  for (const auto &[name, test] : test_list) {
    std::cout << "[TEST] " << name << "... ";

    const TestResult result = test();

    if (result.passed) {
      std::cout << "PASSED (" << result.message << ")\n";
      ++passed;
    } else {
      std::cout << "FAILED (" << result.message << ")\n";
    }

#ifdef _WIN32
    const bool server_running =
        g_server_process.hProcess != nullptr;
#else
    const bool server_running = g_server_pid > 0;
#endif

    if (!server_running &&
        name != "SIGINT Shutdown") {
      std::cout << "Server stopped unexpectedly.\n";
      break;
    }
  }

  stop_server();

  std::cout << "\n=== Result: " << passed
            << "/" << test_list.size()
            << " passed ===\n";

#ifdef _WIN32
  WSACleanup();
#endif

  return passed == static_cast<int>(test_list.size())
             ? 0
             : 1;
}
