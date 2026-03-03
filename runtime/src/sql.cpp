#include "thag/sql.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "thag/db.h"

namespace {

struct SqlBuilderState {
  std::string select_fields;
  std::string from_table;
  std::string where_clause;
  std::string order_by_clause;
  int64_t limit = -1;
  std::string cached_query;
};

std::mutex g_sql_mutex;
std::unordered_map<int, SqlBuilderState> g_sql_builders;
std::atomic<int> g_sql_next_handle{1};

static std::string trim_copy(const std::string& text) {
  std::size_t left = 0;
  while (left < text.size() && std::isspace(static_cast<unsigned char>(text[left]))) {
    ++left;
  }
  std::size_t right = text.size();
  while (right > left && std::isspace(static_cast<unsigned char>(text[right - 1]))) {
    --right;
  }
  return text.substr(left, right - left);
}

static bool set_builder_text(int handle, const char* text, std::string SqlBuilderState::*field) {
  if (text == nullptr) {
    return false;
  }
  const std::string clean = trim_copy(text);
  if (clean.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_sql_mutex);
  auto it = g_sql_builders.find(handle);
  if (it == g_sql_builders.end()) {
    return false;
  }
  it->second.*field = clean;
  it->second.cached_query.clear();
  return true;
}

static std::string build_sql_query(const SqlBuilderState& state) {
  if (state.from_table.empty()) {
    return "";
  }
  const std::string select = state.select_fields.empty() ? "*" : state.select_fields;
  std::string query = "SELECT " + select + " FROM " + state.from_table;
  if (!state.where_clause.empty()) {
    query += " WHERE " + state.where_clause;
  }
  if (!state.order_by_clause.empty()) {
    query += " ORDER BY " + state.order_by_clause;
  }
  if (state.limit >= 0) {
    query += " LIMIT " + std::to_string(state.limit);
  }
  return query;
}

}  // namespace

extern "C" {

int thag_sql_builder_new(void) {
  const int handle = g_sql_next_handle.fetch_add(1);
  std::lock_guard<std::mutex> lock(g_sql_mutex);
  g_sql_builders.emplace(handle, SqlBuilderState{});
  return handle;
}

int thag_sql_builder_select(int handle, const char* fields) {
  return set_builder_text(handle, fields, &SqlBuilderState::select_fields) ? 1 : 0;
}

int thag_sql_builder_from(int handle, const char* table) {
  return set_builder_text(handle, table, &SqlBuilderState::from_table) ? 1 : 0;
}

int thag_sql_builder_where(int handle, const char* predicate) {
  return set_builder_text(handle, predicate, &SqlBuilderState::where_clause) ? 1 : 0;
}

int thag_sql_builder_order_by(int handle, const char* order_by) {
  return set_builder_text(handle, order_by, &SqlBuilderState::order_by_clause) ? 1 : 0;
}

int thag_sql_builder_limit(int handle, int64_t limit) {
  if (limit < 0) {
    return 0;
  }
  std::lock_guard<std::mutex> lock(g_sql_mutex);
  auto it = g_sql_builders.find(handle);
  if (it == g_sql_builders.end()) {
    return 0;
  }
  it->second.limit = limit;
  it->second.cached_query.clear();
  return 1;
}

const char* thag_sql_builder_build(int handle) {
  std::lock_guard<std::mutex> lock(g_sql_mutex);
  auto it = g_sql_builders.find(handle);
  if (it == g_sql_builders.end()) {
    return nullptr;
  }
  it->second.cached_query = build_sql_query(it->second);
  if (it->second.cached_query.empty()) {
    return nullptr;
  }
  return it->second.cached_query.c_str();
}

int thag_sql_builder_reset(int handle) {
  std::lock_guard<std::mutex> lock(g_sql_mutex);
  auto it = g_sql_builders.find(handle);
  if (it == g_sql_builders.end()) {
    return 0;
  }
  it->second = SqlBuilderState{};
  return 1;
}

int thag_sql_builder_free(int handle) {
  std::lock_guard<std::mutex> lock(g_sql_mutex);
  auto it = g_sql_builders.find(handle);
  if (it == g_sql_builders.end()) {
    return 0;
  }
  g_sql_builders.erase(it);
  return 1;
}

int thag_sql_migrate_apply(int db_handle, const char* migration_name, const char* sql) {
  if (migration_name == nullptr || sql == nullptr || std::strlen(migration_name) == 0 || std::strlen(sql) == 0) {
    return 0;
  }
  int result = 0;
  if (!thag_db_client_query(db_handle, sql, &result)) {
    return 0;
  }
  return result > 0 ? 1 : 0;
}

}  // extern "C"
