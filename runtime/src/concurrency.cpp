#include "thag_runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
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
  std::condition_variable done_cv;
  struct TaskItem {
    thag_task_fn fn = nullptr;
    void* user_data = nullptr;
  };
  std::deque<TaskItem> queue;
  std::vector<std::thread> tasks;
  std::atomic<bool> cancelled{false};
  std::atomic<bool> shutdown{false};
  std::atomic<bool> waiting{false};
  std::atomic<int> in_flight{0};
  bool has_deadline = false;
  std::chrono::steady_clock::time_point deadline;
};

struct thag_async_runtime {
  struct TaskItem {
    thag_task_fn fn = nullptr;
    void* user_data = nullptr;
  };
  struct TimerItem {
    Clock::time_point due;
    thag_task_fn fn = nullptr;
    void* user_data = nullptr;
  };

  std::mutex mutex;
  std::condition_variable cv;
  std::condition_variable idle_cv;
  std::deque<TaskItem> ready_queue;
  std::vector<TimerItem> timers;
  std::vector<std::thread> workers;
  std::thread timer_thread;
  std::atomic<bool> shutdown{false};
  std::atomic<int> in_flight{0};
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

static std::size_t default_worker_count() {
  const std::size_t hc = std::thread::hardware_concurrency();
  if (hc == 0) {
    return 2;
  }
  return hc < 2 ? 2 : hc;
}

static void worker_loop(thag_task_scope_t* scope) {
  while (true) {
    thag_task_scope::TaskItem item;
    {
      std::unique_lock<std::mutex> lock(scope->mutex);
      scope->cv.wait(lock, [&]() {
        return scope->shutdown.load() || !scope->queue.empty() || scope->cancelled.load() || timed_out(scope);
      });

      if (scope->shutdown.load()) {
        break;
      }
      if (scope->cancelled.load() || timed_out(scope)) {
        const int dropped = static_cast<int>(scope->queue.size());
        scope->queue.clear();
        if (dropped > 0) {
          scope->in_flight.fetch_sub(dropped);
        }
        scope->done_cv.notify_all();
        continue;
      }
      if (scope->queue.empty()) {
        continue;
      }
      item = scope->queue.front();
      scope->queue.pop_front();
    }

    if (item.fn != nullptr && !scope->cancelled.load() && !timed_out(scope)) {
      item.fn(item.user_data);
    }
    scope->in_flight.fetch_sub(1);
    scope->done_cv.notify_all();
  }
}

static void ensure_workers_started(thag_task_scope_t* scope) {
  if (scope == nullptr || !scope->tasks.empty()) {
    return;
  }
  const std::size_t workers = default_worker_count();
  scope->tasks.reserve(workers);
  for (std::size_t i = 0; i < workers; ++i) {
    scope->tasks.emplace_back([scope]() { worker_loop(scope); });
  }
}

static void stop_and_join_workers(thag_task_scope_t* scope) {
  if (scope == nullptr) {
    return;
  }
  scope->shutdown.store(true);
  scope->cv.notify_all();
  for (auto& t : scope->tasks) {
    if (t.joinable()) {
      t.join();
    }
  }
  scope->tasks.clear();
}

static void async_worker_loop(thag_async_runtime_t* runtime) {
  while (true) {
    thag_async_runtime::TaskItem item;
    {
      std::unique_lock<std::mutex> lock(runtime->mutex);
      runtime->cv.wait(lock, [&]() { return runtime->shutdown.load() || !runtime->ready_queue.empty(); });
      if (runtime->shutdown.load() && runtime->ready_queue.empty()) {
        break;
      }
      if (runtime->ready_queue.empty()) {
        continue;
      }
      item = runtime->ready_queue.front();
      runtime->ready_queue.pop_front();
    }

    if (item.fn != nullptr) {
      item.fn(item.user_data);
    }
    runtime->in_flight.fetch_sub(1);
    runtime->idle_cv.notify_all();
  }
}

static void async_timer_loop(thag_async_runtime_t* runtime) {
  while (!runtime->shutdown.load()) {
    std::vector<thag_async_runtime::TaskItem> due_tasks;
    {
      std::unique_lock<std::mutex> lock(runtime->mutex);
      if (runtime->timers.empty()) {
        runtime->cv.wait_for(lock, std::chrono::milliseconds(5), [&]() {
          return runtime->shutdown.load() || !runtime->timers.empty();
        });
      } else {
        std::sort(runtime->timers.begin(), runtime->timers.end(),
                  [](const auto& a, const auto& b) { return a.due < b.due; });
        const auto next_due = runtime->timers.front().due;
        const auto now = Clock::now();
        if (next_due > now) {
          runtime->cv.wait_until(lock, next_due, [&]() { return runtime->shutdown.load(); });
        }
      }
      if (runtime->shutdown.load()) {
        break;
      }

      const auto now = Clock::now();
      auto it = runtime->timers.begin();
      while (it != runtime->timers.end()) {
        if (it->due <= now) {
          due_tasks.push_back(thag_async_runtime::TaskItem{it->fn, it->user_data});
          it = runtime->timers.erase(it);
          continue;
        }
        ++it;
      }

      if (!due_tasks.empty()) {
        runtime->ready_queue.insert(runtime->ready_queue.end(), due_tasks.begin(), due_tasks.end());
      }
    }
    if (!due_tasks.empty()) {
      runtime->cv.notify_all();
    }
  }
}

static void async_start_workers(thag_async_runtime_t* runtime) {
  if (runtime == nullptr || !runtime->workers.empty()) {
    return;
  }
  const std::size_t count = default_worker_count();
  runtime->workers.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    runtime->workers.emplace_back([runtime]() { async_worker_loop(runtime); });
  }
  runtime->timer_thread = std::thread([runtime]() { async_timer_loop(runtime); });
}

static void async_stop_workers(thag_async_runtime_t* runtime) {
  if (runtime == nullptr) {
    return;
  }
  runtime->shutdown.store(true);
  runtime->cv.notify_all();
  runtime->idle_cv.notify_all();
  if (runtime->timer_thread.joinable()) {
    runtime->timer_thread.join();
  }
  for (auto& worker : runtime->workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  runtime->workers.clear();
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

  ensure_workers_started(scope);
  try {
    std::lock_guard<std::mutex> lock(scope->mutex);
    scope->queue.push_back(thag_task_scope::TaskItem{fn, user_data});
    scope->in_flight.fetch_add(1);
    scope->cv.notify_one();
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
  {
    std::lock_guard<std::mutex> lock(scope->mutex);
    const int dropped = static_cast<int>(scope->queue.size());
    scope->queue.clear();
    if (dropped > 0) {
      scope->in_flight.fetch_sub(dropped);
    }
  }
  scope->cv.notify_all();
  scope->done_cv.notify_all();
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

  std::unique_lock<std::mutex> lock(scope->mutex);
  while (scope->in_flight.load() > 0) {
    if (timed_out(scope)) {
      scope->cancelled.store(true);
      const int dropped = static_cast<int>(scope->queue.size());
      scope->queue.clear();
      if (dropped > 0) {
        scope->in_flight.fetch_sub(dropped);
      }
      break;
    }
    scope->done_cv.wait_for(lock, std::chrono::milliseconds(5));
  }
  lock.unlock();
  stop_and_join_workers(scope);

  return scope->cancelled.load() ? 0 : 1;
}

int thag_task_scope_cancelled(const thag_task_scope_t* scope) {
  if (scope == nullptr) {
    return 1;
  }
  return scope->cancelled.load() ? 1 : 0;
}

thag_async_runtime_t* thag_async_runtime_create(void) {
  auto* runtime = new thag_async_runtime();
  async_start_workers(runtime);
  return runtime;
}

void thag_async_runtime_destroy(thag_async_runtime_t* runtime) {
  if (runtime == nullptr) {
    return;
  }
  (void)thag_async_wait_idle(runtime, 1000);
  async_stop_workers(runtime);
  delete runtime;
}

int thag_async_spawn(thag_async_runtime_t* runtime, thag_task_fn fn, void* user_data) {
  if (runtime == nullptr || fn == nullptr || runtime->shutdown.load()) {
    return 0;
  }
  {
    std::lock_guard<std::mutex> lock(runtime->mutex);
    runtime->ready_queue.push_back(thag_async_runtime::TaskItem{fn, user_data});
    runtime->in_flight.fetch_add(1);
  }
  runtime->cv.notify_one();
  return 1;
}

int thag_async_sleep(thag_async_runtime_t* runtime, uint64_t delay_ms, thag_task_fn fn, void* user_data) {
  if (runtime == nullptr || fn == nullptr || runtime->shutdown.load()) {
    return 0;
  }
  {
    std::lock_guard<std::mutex> lock(runtime->mutex);
    runtime->timers.push_back(thag_async_runtime::TimerItem{
        Clock::now() + std::chrono::milliseconds(delay_ms),
        fn,
        user_data,
    });
    runtime->in_flight.fetch_add(1);
  }
  runtime->cv.notify_all();
  return 1;
}

int thag_async_wait_idle(thag_async_runtime_t* runtime, uint64_t timeout_ms) {
  if (runtime == nullptr) {
    return 0;
  }
  std::unique_lock<std::mutex> lock(runtime->mutex);
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  while (runtime->in_flight.load() > 0) {
    if (runtime->idle_cv.wait_until(lock, deadline) == std::cv_status::timeout) {
      return 0;
    }
  }
  return 1;
}

}  // extern "C"
