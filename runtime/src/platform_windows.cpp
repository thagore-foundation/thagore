#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WinSock2.h>
#include <MSWSock.h>
#include <ws2tcpip.h>

#include <cstdint>

extern "C" {

int thag_platform_event_loop_create(void) {
  HANDLE port = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
  if (port == nullptr) {
    return -1;
  }
  return static_cast<int>(reinterpret_cast<intptr_t>(port));
}

int thag_platform_event_loop_close(int loop_fd) {
  if (loop_fd < 0) {
    return 0;
  }
  HANDLE port = reinterpret_cast<HANDLE>(static_cast<intptr_t>(loop_fd));
  return ::CloseHandle(port) ? 1 : 0;
}

int thag_platform_event_add_read(int loop_fd, int fd, uint64_t user_data) {
  (void)user_data;
  if (loop_fd < 0 || fd < 0) {
    return 0;
  }
  HANDLE port = reinterpret_cast<HANDLE>(static_cast<intptr_t>(loop_fd));
  HANDLE h = reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd));
  return ::CreateIoCompletionPort(h, port, static_cast<ULONG_PTR>(user_data), 0) == nullptr ? 0 : 1;
}

int thag_platform_event_del(int loop_fd, int fd) {
  (void)loop_fd;
  (void)fd;
  return 1;
}

int thag_platform_event_wait(int loop_fd, int timeout_ms, uint64_t* out_user_data, int out_capacity) {
  if (loop_fd < 0 || out_user_data == nullptr || out_capacity <= 0) {
    return -1;
  }
  HANDLE port = reinterpret_cast<HANDLE>(static_cast<intptr_t>(loop_fd));
  DWORD bytes = 0;
  ULONG_PTR key = 0;
  OVERLAPPED* ov = nullptr;
  const DWORD timeout = timeout_ms < 0 ? INFINITE : static_cast<DWORD>(timeout_ms);
  BOOL ok = ::GetQueuedCompletionStatus(port, &bytes, &key, &ov, timeout);
  if (!ok && ov == nullptr) {
    return 0;
  }
  out_user_data[0] = static_cast<uint64_t>(key);
  return 1;
}

int thag_platform_timer_create(void) {
  HANDLE timer = ::CreateWaitableTimer(nullptr, TRUE, nullptr);
  if (timer == nullptr) {
    return -1;
  }
  return static_cast<int>(reinterpret_cast<intptr_t>(timer));
}

int thag_platform_timer_arm(int timer_fd, uint64_t delay_ms) {
  if (timer_fd < 0) {
    return 0;
  }
  HANDLE timer = reinterpret_cast<HANDLE>(static_cast<intptr_t>(timer_fd));
  LARGE_INTEGER due{};
  due.QuadPart = -static_cast<LONGLONG>(delay_ms * 10000ULL);
  return ::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) ? 1 : 0;
}

int thag_platform_timer_read_expirations(int timer_fd, uint64_t* expirations) {
  if (timer_fd < 0 || expirations == nullptr) {
    return 0;
  }
  *expirations = 1;
  return 1;
}

int thag_platform_fd_close(int fd) {
  if (fd < 0) {
    return 0;
  }
  HANDLE h = reinterpret_cast<HANDLE>(static_cast<intptr_t>(fd));
  return ::CloseHandle(h) ? 1 : 0;
}

int thag_platform_file_open_readonly(const char* path) {
  if (path == nullptr || *path == '\0') {
    return -1;
  }
  HANDLE h = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }
  return static_cast<int>(reinterpret_cast<intptr_t>(h));
}

int thag_platform_socket_tcp(void) {
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    return -1;
  }
  return static_cast<int>(s);
}

int thag_platform_signal_ignore_pipe(void) {
  return 1;
}

}  // extern "C"

#endif
