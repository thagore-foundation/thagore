#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace thagc::query {

inline constexpr std::string_view kParseFileQueryName = "parse_file";
inline constexpr std::string_view kNameResolveQueryName = "name_resolve";
inline constexpr std::string_view kTypeCheckFunctionQueryName = "type_check_fn";
inline constexpr std::string_view kBorrowCheckFunctionQueryName = "borrow_check_fn";
inline constexpr std::string_view kMonomorphizeQueryName = "monomorphize";
inline constexpr std::string_view kCodegenFunctionQueryName = "codegen_fn";
inline constexpr std::string_view kLinkQueryName = "link";

template <typename K, typename V, typename Hasher = std::hash<K>, typename Eq = std::equal_to<K>>
class QueryCache {
 public:
  std::optional<V> get(const K& key, std::uint64_t input_hash) const {
    const auto hash_it = dep_hash_.find(key);
    if (hash_it == dep_hash_.end() || hash_it->second != input_hash) {
      return std::nullopt;
    }
    const auto value_it = results_.find(key);
    if (value_it == results_.end()) {
      return std::nullopt;
    }
    return value_it->second;
  }

  void put(const K& key, V value, std::uint64_t input_hash) {
    results_[key] = std::move(value);
    dep_hash_[key] = input_hash;
  }

  void clear() {
    results_.clear();
    dep_hash_.clear();
  }

  std::size_t size() const {
    return results_.size();
  }

 private:
  std::unordered_map<K, V, Hasher, Eq> results_;
  std::unordered_map<K, std::uint64_t, Hasher, Eq> dep_hash_;
};

}  // namespace thagc::query
