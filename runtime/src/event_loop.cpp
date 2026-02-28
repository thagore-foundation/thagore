#include "thag/event_loop.h"

#include <array>
#include <new>

extern "C" {
int thag_platform_event_loop_create(void);
int thag_platform_event_loop_close(int loop_fd);
int thag_platform_event_add_read(int loop_fd, int fd, uint64_t user_data);
int thag_platform_event_del(int loop_fd, int fd);
int thag_platform_event_wait(int loop_fd, int timeout_ms, uint64_t* out_user_data, int out_capacity);
}

struct thag_event_loop {
  int backend_fd = -1;
};

namespace {

constexpr int kDrainBatchSize = 64;

void drain_backend_events(int backend_fd) {
  std::array<uint64_t, kDrainBatchSize> drained{};
  while (true) {
    const int ready = thag_platform_event_wait(backend_fd, 0, drained.data(), static_cast<int>(drained.size()));
    if (ready <= 0) {
      return;
    }
  }
}

}  // namespace

extern "C" {

thag_event_loop_t* thag_event_loop_create(void) {
  thag_event_loop_t* loop = new (std::nothrow) thag_event_loop_t();
  if (loop == nullptr) {
    return nullptr;
  }
  loop->backend_fd = thag_platform_event_loop_create();
  if (loop->backend_fd < 0) {
    delete loop;
    return nullptr;
  }
  return loop;
}

void thag_event_loop_destroy(thag_event_loop_t* loop) {
  if (loop == nullptr) {
    return;
  }
  if (loop->backend_fd >= 0) {
    drain_backend_events(loop->backend_fd);
    (void)thag_platform_event_loop_close(loop->backend_fd);
    loop->backend_fd = -1;
  }
  delete loop;
}

int thag_event_loop_register_read(thag_event_loop_t* loop, int fd, uint64_t user_data) {
  if (loop == nullptr || loop->backend_fd < 0 || fd < 0) {
    return 0;
  }
  return thag_platform_event_add_read(loop->backend_fd, fd, user_data);
}

int thag_event_loop_unregister_read(thag_event_loop_t* loop, int fd) {
  if (loop == nullptr || loop->backend_fd < 0 || fd < 0) {
    return 0;
  }
  return thag_platform_event_del(loop->backend_fd, fd);
}

int thag_event_loop_poll(thag_event_loop_t* loop, int timeout_ms, uint64_t* out_user_data, int out_capacity) {
  if (loop == nullptr || loop->backend_fd < 0) {
    return -1;
  }
  if (timeout_ms < -1 || out_capacity < 0) {
    return -1;
  }
  if (out_capacity == 0) {
    return 0;
  }
  if (out_user_data == nullptr) {
    return -1;
  }
  return thag_platform_event_wait(loop->backend_fd, timeout_ms, out_user_data, out_capacity);
}

}  // extern "C"
