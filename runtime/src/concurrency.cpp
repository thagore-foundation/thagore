#include "thag_runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

extern "C" {
int thag_platform_event_loop_create(void);
int thag_platform_event_loop_close(int loop_fd);
int thag_platform_event_add_read(int loop_fd, int fd, uint64_t user_data);
int thag_platform_event_del(int loop_fd, int fd);
int thag_platform_event_wait(int loop_fd, int timeout_ms, uint64_t* out_user_data, int out_capacity);
int thag_platform_timer_create(void);
int thag_platform_timer_arm(int timer_fd, uint64_t delay_ms);
int thag_platform_timer_read_expirations(int timer_fd, uint64_t* expirations);
int thag_platform_fd_close(int fd);
}

struct thag_task_scope {
  mutable std::mutex mutex;
  std::condition_variable cv;
  std::atomic<bool> cancelled{false};
  std::atomic<int> in_flight{0};
  bool waiting = false;
  bool closed = false;
  bool completed = false;
  bool has_deadline = false;
  std::chrono::steady_clock::time_point deadline{};
  thag_task_scope_t* parent = nullptr;
  std::vector<thag_task_scope_t*> children;
};

struct thag_async_runtime {
  mutable std::mutex mutex;
  std::condition_variable cv;
  std::atomic<int> in_flight{0};
  std::atomic<bool> closed{false};
};

namespace {

using Clock = std::chrono::steady_clock;

struct SchedulerTask {
  thag_task_fn fn = nullptr;
  void* user_data = nullptr;
  thag_task_scope_t* scope = nullptr;
  thag_async_runtime_t* runtime = nullptr;
};

struct TimerTask {
  int timer_fd = -1;
  SchedulerTask task;
};

struct SchedulerState {
  std::mutex mutex;
  std::condition_variable cv;
  std::condition_variable queue_space_cv;
  std::deque<SchedulerTask> ready;
  std::vector<std::thread> workers;
  std::thread event_thread;
  std::atomic<bool> shutdown{false};
  std::size_t queue_limit = 8192;
  int event_loop_fd = -1;

  std::mutex timers_mutex;
  std::unordered_map<int, TimerTask> timers;
};

std::once_flag g_scheduler_once;
SchedulerState* g_scheduler = nullptr;

thread_local thag_task_scope_t* g_current_scope = nullptr;

static std::size_t default_worker_count() {
  const std::size_t hc = std::thread::hardware_concurrency();
  if (hc == 0) {
    return 2;
  }
  return hc < 2 ? 2 : hc;
}

static bool scope_timed_out(thag_task_scope_t* scope) {
  if (scope == nullptr || !scope->has_deadline) {
    return false;
  }
  if (Clock::now() < scope->deadline) {
    return false;
  }
  scope->cancelled.store(true);
  return true;
}

static void notify_scope_change(thag_task_scope_t* scope) {
  if (scope == nullptr) {
    return;
  }
  scope->cv.notify_all();
  thag_task_scope_t* parent = scope->parent;
  if (parent != nullptr) {
    parent->cv.notify_all();
  }
}

static void notify_runtime_change(thag_async_runtime_t* runtime) {
  if (runtime == nullptr) {
    return;
  }
  runtime->cv.notify_all();
}

static void complete_task(const SchedulerTask& task) {
  if (task.scope != nullptr) {
    task.scope->in_flight.fetch_sub(1);
    notify_scope_change(task.scope);
  }
  if (task.runtime != nullptr) {
    task.runtime->in_flight.fetch_sub(1);
    notify_runtime_change(task.runtime);
  }
}

static void scheduler_worker_loop(SchedulerState* scheduler) {
  while (true) {
    SchedulerTask task;
    {
      std::unique_lock<std::mutex> lock(scheduler->mutex);
      scheduler->cv.wait(lock, [&]() { return scheduler->shutdown.load() || !scheduler->ready.empty(); });
      if (scheduler->shutdown.load() && scheduler->ready.empty()) {
        break;
      }
      if (scheduler->ready.empty()) {
        continue;
      }
      task = scheduler->ready.front();
      scheduler->ready.pop_front();
      scheduler->queue_space_cv.notify_all();
    }

    thag_task_scope_t* previous_scope = g_current_scope;
    g_current_scope = task.scope;
    const bool cancelled = task.scope != nullptr && (task.scope->cancelled.load() || scope_timed_out(task.scope));
    if (!cancelled && task.fn != nullptr) {
      task.fn(task.user_data);
    }
    g_current_scope = previous_scope;
    complete_task(task);
  }
}

static bool scheduler_submit(SchedulerTask task, bool block_on_full_queue) {
  if (g_scheduler == nullptr) {
    return false;
  }
  std::unique_lock<std::mutex> lock(g_scheduler->mutex);
  if (block_on_full_queue) {
    g_scheduler->queue_space_cv.wait(lock, [&]() {
      return g_scheduler->shutdown.load() || g_scheduler->ready.size() < g_scheduler->queue_limit;
    });
  } else if (g_scheduler->ready.size() >= g_scheduler->queue_limit) {
    return false;
  }
  if (g_scheduler->shutdown.load()) {
    return false;
  }
  g_scheduler->ready.push_back(task);
  g_scheduler->cv.notify_one();
  return true;
}

static void scheduler_cancel_runtime_timers(thag_async_runtime_t* runtime) {
  if (g_scheduler == nullptr || runtime == nullptr) {
    return;
  }
  std::vector<TimerTask> dropped;
  {
    std::lock_guard<std::mutex> lock(g_scheduler->timers_mutex);
    for (auto it = g_scheduler->timers.begin(); it != g_scheduler->timers.end();) {
      if (it->second.task.runtime == runtime) {
        dropped.push_back(it->second);
        it = g_scheduler->timers.erase(it);
        continue;
      }
      ++it;
    }
  }
  for (const auto& timer : dropped) {
    if (g_scheduler->event_loop_fd >= 0) {
      (void)thag_platform_event_del(g_scheduler->event_loop_fd, timer.timer_fd);
    }
    (void)thag_platform_fd_close(timer.timer_fd);
    complete_task(timer.task);
  }
}

static bool scheduler_register_timer(SchedulerTask task, uint64_t delay_ms) {
  if (g_scheduler == nullptr || g_scheduler->event_loop_fd < 0) {
    return false;
  }
  const int timer_fd = thag_platform_timer_create();
  if (timer_fd < 0) {
    return false;
  }
  if (!thag_platform_timer_arm(timer_fd, delay_ms)) {
    (void)thag_platform_fd_close(timer_fd);
    return false;
  }
  if (!thag_platform_event_add_read(g_scheduler->event_loop_fd, timer_fd, static_cast<uint64_t>(timer_fd))) {
    (void)thag_platform_fd_close(timer_fd);
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_scheduler->timers_mutex);
    g_scheduler->timers[timer_fd] = TimerTask{timer_fd, task};
  }
  return true;
}

static void scheduler_event_loop(SchedulerState* scheduler) {
  while (!scheduler->shutdown.load()) {
    if (scheduler->event_loop_fd < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    uint64_t ids[128] = {};
    const int ready = thag_platform_event_wait(scheduler->event_loop_fd, 50, ids, 128);
    if (ready <= 0) {
      continue;
    }
    for (int i = 0; i < ready; ++i) {
      const int timer_fd = static_cast<int>(ids[i]);
      TimerTask timer{};
      bool found = false;
      {
        std::lock_guard<std::mutex> lock(scheduler->timers_mutex);
        auto it = scheduler->timers.find(timer_fd);
        if (it != scheduler->timers.end()) {
          timer = it->second;
          scheduler->timers.erase(it);
          found = true;
        }
      }
      if (!found) {
        continue;
      }
      uint64_t expirations = 0;
      (void)thag_platform_timer_read_expirations(timer_fd, &expirations);
      (void)thag_platform_event_del(scheduler->event_loop_fd, timer_fd);
      (void)thag_platform_fd_close(timer_fd);
      if (!scheduler_submit(timer.task, true)) {
        complete_task(timer.task);
      }
    }
  }

  std::vector<TimerTask> pending;
  {
    std::lock_guard<std::mutex> lock(scheduler->timers_mutex);
    for (const auto& [_, timer] : scheduler->timers) {
      pending.push_back(timer);
    }
    scheduler->timers.clear();
  }
  for (const auto& timer : pending) {
    if (scheduler->event_loop_fd >= 0) {
      (void)thag_platform_event_del(scheduler->event_loop_fd, timer.timer_fd);
    }
    (void)thag_platform_fd_close(timer.timer_fd);
    complete_task(timer.task);
  }
}

static void scheduler_start_once() {
  std::call_once(g_scheduler_once, []() {
    auto* scheduler = new SchedulerState();
    scheduler->event_loop_fd = thag_platform_event_loop_create();
    const std::size_t worker_count = default_worker_count();
    scheduler->workers.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
      scheduler->workers.emplace_back([scheduler]() { scheduler_worker_loop(scheduler); });
    }
    scheduler->event_thread = std::thread([scheduler]() { scheduler_event_loop(scheduler); });
    g_scheduler = scheduler;
  });
}

static void scheduler_shutdown() {
  if (g_scheduler == nullptr) {
    return;
  }
  g_scheduler->shutdown.store(true);
  g_scheduler->cv.notify_all();
  g_scheduler->queue_space_cv.notify_all();
  if (g_scheduler->event_thread.joinable()) {
    g_scheduler->event_thread.join();
  }
  for (auto& worker : g_scheduler->workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  std::deque<SchedulerTask> dropped;
  {
    std::lock_guard<std::mutex> lock(g_scheduler->mutex);
    dropped.swap(g_scheduler->ready);
  }
  for (const auto& task : dropped) {
    complete_task(task);
  }
  (void)thag_platform_event_loop_close(g_scheduler->event_loop_fd);
  delete g_scheduler;
  g_scheduler = nullptr;
}

static std::vector<thag_task_scope_t*> scope_children_snapshot(thag_task_scope_t* scope) {
  std::lock_guard<std::mutex> lock(scope->mutex);
  return scope->children;
}

static void scope_detach_parent(thag_task_scope_t* scope) {
  if (scope == nullptr || scope->parent == nullptr) {
    return;
  }
  thag_task_scope_t* parent = scope->parent;
  {
    std::lock_guard<std::mutex> lock(parent->mutex);
    parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), scope), parent->children.end());
  }
  scope->parent = nullptr;
  notify_scope_change(parent);
}

static void scope_cancel_recursive(thag_task_scope_t* scope) {
  if (scope == nullptr) {
    return;
  }
  const bool already_cancelled = scope->cancelled.exchange(true);
  if (already_cancelled) {
    return;
  }
  const auto children = scope_children_snapshot(scope);
  for (thag_task_scope_t* child : children) {
    scope_cancel_recursive(child);
  }
  notify_scope_change(scope);
}

}  // namespace

extern "C" {

void thag_concurrency_runtime_init(void) {
  scheduler_start_once();
}

void thag_concurrency_runtime_shutdown(void) {
  scheduler_shutdown();
}

thag_task_scope_t* thag_task_scope_create(void) {
  scheduler_start_once();
  auto* scope = new thag_task_scope();
  thag_task_scope_t* parent = g_current_scope;
  if (parent != nullptr) {
    scope->parent = parent;
    std::lock_guard<std::mutex> lock(parent->mutex);
    parent->children.push_back(scope);
    notify_scope_change(parent);
  }
  return scope;
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
  scope_detach_parent(scope);
  delete scope;
}

int thag_task_scope_spawn(thag_task_scope_t* scope, thag_task_fn fn, void* user_data) {
  if (scope == nullptr || fn == nullptr || g_scheduler == nullptr) {
    return 0;
  }
  if (scope->cancelled.load() || scope_timed_out(scope)) {
    scope->cancelled.store(true);
    return 0;
  }
  {
    std::lock_guard<std::mutex> lock(scope->mutex);
    if (scope->waiting || scope->closed) {
      return 0;
    }
  }

  scope->in_flight.fetch_add(1);
  SchedulerTask task;
  task.fn = fn;
  task.user_data = user_data;
  task.scope = scope;
  task.runtime = nullptr;
  if (!scheduler_submit(task, true)) {
    scope->in_flight.fetch_sub(1);
    notify_scope_change(scope);
    return 0;
  }
  return 1;
}

void thag_task_scope_cancel(thag_task_scope_t* scope) {
  scope_cancel_recursive(scope);
}

void thag_task_scope_set_timeout_ms(thag_task_scope_t* scope, uint64_t timeout_ms) {
  if (scope == nullptr) {
    return;
  }
  scope->deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  scope->has_deadline = true;
  notify_scope_change(scope);
}

int thag_task_scope_wait(thag_task_scope_t* scope) {
  if (scope == nullptr) {
    return 0;
  }
  {
    std::lock_guard<std::mutex> lock(scope->mutex);
    scope->waiting = true;
    scope->closed = true;
  }

  while (scope->in_flight.load() > 0) {
    if (scope_timed_out(scope)) {
      scope_cancel_recursive(scope);
    }
    std::unique_lock<std::mutex> lock(scope->mutex);
    scope->cv.wait_for(lock, std::chrono::milliseconds(2));
  }

  while (true) {
    auto children = scope_children_snapshot(scope);
    if (children.empty()) {
      break;
    }
    for (thag_task_scope_t* child : children) {
      if (child == nullptr) {
        continue;
      }
      scope_cancel_recursive(child);
      (void)thag_task_scope_wait(child);
      scope_detach_parent(child);
    }
    {
      std::lock_guard<std::mutex> lock(scope->mutex);
      scope->children.erase(
          std::remove_if(scope->children.begin(), scope->children.end(),
                         [](thag_task_scope_t* child) { return child == nullptr || child->completed; }),
          scope->children.end());
      if (scope->children.empty()) {
        break;
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(scope->mutex);
    scope->completed = true;
    scope->waiting = false;
  }
  notify_scope_change(scope);
  return scope->cancelled.load() ? 0 : 1;
}

int thag_task_scope_cancelled(const thag_task_scope_t* scope) {
  if (scope == nullptr) {
    return 1;
  }
  return scope->cancelled.load() ? 1 : 0;
}

int thag_task_is_cancelled(void) {
  return (g_current_scope != nullptr && g_current_scope->cancelled.load()) ? 1 : 0;
}

thag_async_runtime_t* thag_async_runtime_create(void) {
  scheduler_start_once();
  return new thag_async_runtime();
}

void thag_async_runtime_destroy(thag_async_runtime_t* runtime) {
  if (runtime == nullptr) {
    return;
  }
  runtime->closed.store(true);
  scheduler_cancel_runtime_timers(runtime);
  (void)thag_async_wait_idle(runtime, 2000);
  delete runtime;
}

int thag_async_spawn(thag_async_runtime_t* runtime, thag_task_fn fn, void* user_data) {
  if (runtime == nullptr || fn == nullptr || runtime->closed.load()) {
    return 0;
  }
  runtime->in_flight.fetch_add(1);
  SchedulerTask task;
  task.fn = fn;
  task.user_data = user_data;
  task.scope = nullptr;
  task.runtime = runtime;
  if (!scheduler_submit(task, true)) {
    runtime->in_flight.fetch_sub(1);
    notify_runtime_change(runtime);
    return 0;
  }
  return 1;
}

int thag_async_sleep(thag_async_runtime_t* runtime, uint64_t delay_ms, thag_task_fn fn, void* user_data) {
  if (runtime == nullptr || fn == nullptr || runtime->closed.load()) {
    return 0;
  }
  runtime->in_flight.fetch_add(1);
  SchedulerTask task;
  task.fn = fn;
  task.user_data = user_data;
  task.scope = nullptr;
  task.runtime = runtime;
  if (!scheduler_register_timer(task, delay_ms)) {
    runtime->in_flight.fetch_sub(1);
    notify_runtime_change(runtime);
    return 0;
  }
  return 1;
}

int thag_async_wait_idle(thag_async_runtime_t* runtime, uint64_t timeout_ms) {
  if (runtime == nullptr) {
    return 0;
  }
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  std::unique_lock<std::mutex> lock(runtime->mutex);
  while (runtime->in_flight.load() > 0) {
    if (runtime->cv.wait_until(lock, deadline) == std::cv_status::timeout) {
      return 0;
    }
  }
  return 1;
}

}  // extern "C"
