#include "thag/db.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#if defined(THAG_RUNTIME_HAS_SQLITE3)
#include <sqlite3.h>
#endif

namespace {

struct DbHandle {
  std::string dsn;
#if defined(THAG_RUNTIME_HAS_SQLITE3)
  sqlite3* sqlite = nullptr;
#endif
};

std::mutex g_db_mutex;
std::unordered_map<int, DbHandle> g_db_handles;
std::atomic<int> g_db_next_handle{1};

std::string lower_copy(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text;
}

bool is_select_query(const char* sql) {
  if (sql == nullptr) {
    return false;
  }
  std::string trimmed(sql);
  const std::size_t left = trimmed.find_first_not_of(" \t\r\n");
  if (left == std::string::npos) {
    return false;
  }
  trimmed = trimmed.substr(left);
  return lower_copy(trimmed).rfind("select", 0) == 0;
}

#if defined(THAG_RUNTIME_HAS_SQLITE3)
sqlite3* open_sqlite_from_dsn(const char* dsn) {
  if (dsn == nullptr || *dsn == '\0') {
    return nullptr;
  }
  std::string path(dsn);
  if (path == "memory://") {
    path = ":memory:";
  } else if (path.rfind("sqlite://", 0) == 0) {
    path = path.substr(9);
  }
  if (path.empty()) {
    return nullptr;
  }

  sqlite3* db = nullptr;
  if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
    if (db != nullptr) {
      sqlite3_close(db);
    }
    return nullptr;
  }
  return db;
}
#endif

}  // namespace

extern "C" {

int thag_db_client_connect(const char* path, int* out_handle) {
  if (out_handle == nullptr) {
    return 0;
  }
  *out_handle = 0;
  if (path == nullptr || std::strlen(path) == 0) {
    return 0;
  }

  DbHandle handle_data;
  handle_data.dsn = path;

#if defined(THAG_RUNTIME_HAS_SQLITE3)
  handle_data.sqlite = open_sqlite_from_dsn(path);
  if (handle_data.sqlite == nullptr) {
    return 0;
  }
#endif

  const int handle = g_db_next_handle.fetch_add(1);
  {
    std::lock_guard<std::mutex> lock(g_db_mutex);
    g_db_handles[handle] = std::move(handle_data);
  }
  *out_handle = handle;
  return 1;
}

int thag_db_client_query(int handle, const char* sql, int* out_result) {
  if (out_result == nullptr) {
    return 0;
  }
  *out_result = 0;
  if (sql == nullptr || std::strlen(sql) == 0) {
    return 0;
  }

  std::lock_guard<std::mutex> lock(g_db_mutex);
  auto it = g_db_handles.find(handle);
  if (it == g_db_handles.end()) {
    return 0;
  }

#if defined(THAG_RUNTIME_HAS_SQLITE3)
  if (it->second.sqlite != nullptr) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(it->second.sqlite, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      if (stmt != nullptr) {
        sqlite3_finalize(stmt);
      }
      return 0;
    }

    const int step_rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step_rc != SQLITE_ROW && step_rc != SQLITE_DONE) {
      return 0;
    }
    *out_result = is_select_query(sql) ? 1 : 2;
    return 1;
  }
#endif

  *out_result = is_select_query(sql) ? 1 : 2;
  return 1;
}

int thag_db_client_close(int handle) {
  std::lock_guard<std::mutex> lock(g_db_mutex);
  auto it = g_db_handles.find(handle);
  if (it == g_db_handles.end()) {
    return 0;
  }
#if defined(THAG_RUNTIME_HAS_SQLITE3)
  if (it->second.sqlite != nullptr) {
    sqlite3_close(it->second.sqlite);
    it->second.sqlite = nullptr;
  }
#endif
  g_db_handles.erase(it);
  return 1;
}

}  // extern "C"
