#include "thag_runtime.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

static bool timed_out(thag_task_scope_t* scope);

}  // namespace

struct thag_task_scope {
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::thread> tasks;
  std::atomic<bool> cancelled{false};
  std::atomic<bool> waiting{false};
  std::atomic<int> in_flight{0};
  bool has_deadline = false;
  std::chrono::steady_clock::time_point deadline;
};

namespace {

static bool timed_out(thag_task_scope_t* scope) {
  if (scope == nullptr || !scope->has_deadline) {
    return false;
  }
  if (Clock::now() < scope->deadline) {
    return false;
  }
  scope->cancelled.store(true);
  return true;
}

}  // namespace

extern "C" {

thag_task_scope_t* thag_task_scope_create(void) {
  return new thag_task_scope();
}

thag_task_scope_t* thag_nursery_create(void) {
  return thag_task_scope_create();
}

void thag_task_scope_destroy(thag_task_scope_t* scope) {
  if (scope == nullptr) {
    return;
  }
  thag_task_scope_cancel(scope);
  (void)thag_task_scope_wait(scope);
  delete scope;
}

int thag_task_scope_spawn(thag_task_scope_t* scope, thag_task_fn fn, void* user_data) {
  if (scope == nullptr || fn == nullptr) {
    return 0;
  }
  if (scope->waiting.load() || scope->cancelled.load() || timed_out(scope)) {
    return 0;
  }

  scope->in_flight.fetch_add(1);
  try {
    std::lock_guard<std::mutex> lock(scope->mutex);
    scope->tasks.emplace_back([scope, fn, user_data]() {
      if (!scope->cancelled.load() && !timed_out(scope)) {
        fn(user_data);
      }
      scope->in_flight.fetch_sub(1);
      scope->cv.notify_all();
    });
  } catch (...) {
    scope->in_flight.fetch_sub(1);
    return 0;
  }
  return 1;
}

void thag_task_scope_cancel(thag_task_scope_t* scope) {
  if (scope == nullptr) {
    return;
  }
  scope->cancelled.store(true);
  scope->cv.notify_all();
}

void thag_task_scope_set_timeout_ms(thag_task_scope_t* scope, uint64_t timeout_ms) {
  if (scope == nullptr) {
    return;
  }
  scope->deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  scope->has_deadline = true;
}

int thag_task_scope_wait(thag_task_scope_t* scope) {
  if (scope == nullptr) {
    return 0;
  }
  scope->waiting.store(true);

  while (true) {
    if (timed_out(scope)) {
      scope->cancelled.store(true);
    }

    std::thread task;
    {
      std::unique_lock<std::mutex> lock(scope->mutex);
      if (scope->tasks.empty()) {
        if (scope->in_flight.load() == 0) {
          break;
        }
        scope->cv.wait_for(lock, std::chrono::milliseconds(5));
        continue;
      }
      task = std::move(scope->tasks.back());
      scope->tasks.pop_back();
    }

    if (task.joinable()) {
      task.join();
    }
  }

  return scope->cancelled.load() ? 0 : 1;
}

int thag_task_scope_cancelled(const thag_task_scope_t* scope) {
  if (scope == nullptr) {
    return 1;
  }
  return scope->cancelled.load() ? 1 : 0;
}

}  // extern "C"
