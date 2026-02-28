#if !defined(_WIN32)

#include <cerrno>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>

#if defined(__linux__)
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
namespace {

constexpr int kSyntheticTimerBase = 1 << 20;
std::atomic<int> g_next_timer_id{kSyntheticTimerBase};
std::mutex g_timer_mutex;
std::unordered_map<int, uint64_t> g_timer_delays_ms;

bool is_synthetic_timer(int fd) {
  return fd >= kSyntheticTimerBase;
}

}  // namespace
#endif

extern "C" {

int thag_platform_event_loop_create(void) {
#if defined(__linux__)
  return ::epoll_create1(EPOLL_CLOEXEC);
#elif defined(__APPLE__)
  return ::kqueue();
#else
  return -1;
#endif
}

int thag_platform_event_loop_close(int loop_fd) {
  if (loop_fd < 0) {
    return 0;
  }
#if defined(__linux__) || defined(__APPLE__)
  return ::close(loop_fd) == 0 ? 1 : 0;
#else
  return 0;
#endif
}

int thag_platform_event_add_read(int loop_fd, int fd, uint64_t user_data) {
  if (loop_fd < 0 || fd < 0) {
    return 0;
  }
#if defined(__linux__)
  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.u64 = user_data;
  return ::epoll_ctl(loop_fd, EPOLL_CTL_ADD, fd, &ev) == 0 ? 1 : 0;
#elif defined(__APPLE__)
  if (is_synthetic_timer(fd)) {
    uint64_t delay_ms = 1;
    {
      std::lock_guard<std::mutex> lock(g_timer_mutex);
      auto it = g_timer_delays_ms.find(fd);
      if (it != g_timer_delays_ms.end()) {
        delay_ms = it->second == 0 ? 1 : it->second;
      }
    }
    struct kevent kev{};
    int16_t timer_flags = 0;
    intptr_t timer_data = static_cast<intptr_t>(delay_ms);
#if defined(NOTE_MSECONDS)
    timer_flags = NOTE_MSECONDS;
#elif defined(NOTE_USECONDS)
    timer_flags = NOTE_USECONDS;
    timer_data = static_cast<intptr_t>(delay_ms * 1000);
#endif
    EV_SET(&kev, static_cast<uintptr_t>(fd), EVFILT_TIMER, EV_ADD | EV_ENABLE | EV_ONESHOT, timer_flags, timer_data,
           reinterpret_cast<void*>(static_cast<uintptr_t>(user_data)));
    return ::kevent(loop_fd, &kev, 1, nullptr, 0, nullptr) == 0 ? 1 : 0;
  }
  struct kevent kev{};
  EV_SET(&kev, static_cast<uintptr_t>(fd), EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0,
         reinterpret_cast<void*>(static_cast<uintptr_t>(user_data)));
  return ::kevent(loop_fd, &kev, 1, nullptr, 0, nullptr) == 0 ? 1 : 0;
#else
  (void)user_data;
  return 0;
#endif
}

int thag_platform_event_del(int loop_fd, int fd) {
  if (loop_fd < 0 || fd < 0) {
    return 0;
  }
#if defined(__linux__)
  return ::epoll_ctl(loop_fd, EPOLL_CTL_DEL, fd, nullptr) == 0 ? 1 : 0;
#elif defined(__APPLE__)
  struct kevent kev{};
  const int16_t filter = is_synthetic_timer(fd) ? EVFILT_TIMER : EVFILT_READ;
  EV_SET(&kev, static_cast<uintptr_t>(fd), filter, EV_DELETE, 0, 0, nullptr);
  return ::kevent(loop_fd, &kev, 1, nullptr, 0, nullptr) == 0 ? 1 : 0;
#else
  return 0;
#endif
}

int thag_platform_event_wait(int loop_fd, int timeout_ms, uint64_t* out_user_data, int out_capacity) {
  if (loop_fd < 0 || out_user_data == nullptr || out_capacity <= 0) {
    return -1;
  }
#if defined(__linux__)
  std::vector<epoll_event> events(static_cast<std::size_t>(out_capacity));
  const int ready = ::epoll_wait(loop_fd, events.data(), out_capacity, timeout_ms);
  if (ready <= 0) {
    return ready;
  }
  for (int i = 0; i < ready; ++i) {
    out_user_data[i] = events[static_cast<std::size_t>(i)].data.u64;
  }
  return ready;
#elif defined(__APPLE__)
  std::vector<struct kevent> events(static_cast<std::size_t>(out_capacity));
  struct timespec ts{};
  struct timespec* ts_ptr = nullptr;
  if (timeout_ms >= 0) {
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = static_cast<long>((timeout_ms % 1000) * 1000000);
    ts_ptr = &ts;
  }
  const int ready = ::kevent(loop_fd, nullptr, 0, events.data(), out_capacity, ts_ptr);
  if (ready <= 0) {
    return ready;
  }
  for (int i = 0; i < ready; ++i) {
    out_user_data[i] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(events[static_cast<std::size_t>(i)].udata));
  }
  return ready;
#else
  (void)timeout_ms;
  return -1;
#endif
}

int thag_platform_timer_create(void) {
#if defined(__linux__)
  return ::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
#elif defined(__APPLE__)
  const int timer_id = g_next_timer_id.fetch_add(1);
  {
    std::lock_guard<std::mutex> lock(g_timer_mutex);
    g_timer_delays_ms[timer_id] = 1;
  }
  return timer_id;
#else
  return -1;
#endif
}

int thag_platform_timer_arm(int timer_fd, uint64_t delay_ms) {
  if (timer_fd < 0) {
    return 0;
  }
#if defined(__linux__)
  itimerspec spec{};
  spec.it_value.tv_sec = static_cast<time_t>(delay_ms / 1000);
  spec.it_value.tv_nsec = static_cast<long>((delay_ms % 1000) * 1000000);
  return ::timerfd_settime(timer_fd, 0, &spec, nullptr) == 0 ? 1 : 0;
#elif defined(__APPLE__)
  if (!is_synthetic_timer(timer_fd)) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_timer_mutex);
  g_timer_delays_ms[timer_fd] = delay_ms == 0 ? 1 : delay_ms;
  return 1;
#else
  (void)delay_ms;
  return 0;
#endif
}

int thag_platform_timer_read_expirations(int timer_fd, uint64_t* expirations) {
  if (timer_fd < 0 || expirations == nullptr) {
    return 0;
  }
#if defined(__linux__)
  uint64_t local = 0;
  const ssize_t n = ::read(timer_fd, &local, sizeof(local));
  if (n != static_cast<ssize_t>(sizeof(local))) {
    return 0;
  }
  *expirations = local;
  return 1;
#else
  *expirations = 1;
  return 1;
#endif
}

int thag_platform_fd_close(int fd) {
  if (fd < 0) {
    return 0;
  }
#if defined(__linux__) || defined(__APPLE__)
  #if defined(__APPLE__)
  if (is_synthetic_timer(fd)) {
    std::lock_guard<std::mutex> lock(g_timer_mutex);
    g_timer_delays_ms.erase(fd);
    return 1;
  }
  #endif
  return ::close(fd) == 0 ? 1 : 0;
#else
  return 0;
#endif
}

int thag_platform_file_open_readonly(const char* path) {
#if defined(__linux__) || defined(__APPLE__)
  if (path == nullptr || *path == '\0') {
    return -1;
  }
  return ::open(path, O_RDONLY);
#else
  (void)path;
  return -1;
#endif
}

int thag_platform_socket_tcp(void) {
#if defined(__linux__) || defined(__APPLE__)
  return ::socket(AF_INET, SOCK_STREAM, 0);
#else
  return -1;
#endif
}

int thag_platform_signal_ignore_pipe(void) {
#if defined(__linux__) || defined(__APPLE__)
  return ::signal(SIGPIPE, SIG_IGN) == SIG_ERR ? 0 : 1;
#else
  return 0;
#endif
}

}  // extern "C"

#endif
