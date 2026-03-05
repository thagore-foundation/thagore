#include "thagc/frontend/source_map.hpp"

#include <algorithm>

namespace thagc::syntax {

std::vector<std::uint32_t> SourceMap::compute_line_starts(const std::string& src) {
  std::vector<std::uint32_t> out;
  out.push_back(0);
  for (std::uint32_t i = 0; i < src.size(); ++i) {
    if (src[i] == '\n') {
      out.push_back(i + 1);
    }
  }
  return out;
}

const SourceMap::FileEntry* SourceMap::file(std::uint32_t file_id) const {
  if (file_id >= files_.size()) {
    return nullptr;
  }
  return &files_[file_id];
}

std::uint32_t SourceMap::clamp_offset(std::uint32_t file_id, std::uint32_t offset) const {
  const FileEntry* entry = file(file_id);
  if (entry == nullptr) {
    return 0;
  }
  const std::uint32_t n = static_cast<std::uint32_t>(entry->src.size());
  if (offset > n) {
    return n;
  }
  return offset;
}

std::uint32_t SourceMap::add_file(std::string path, std::string src) {
  FileEntry entry;
  entry.path = std::move(path);
  entry.line_starts = compute_line_starts(src);
  entry.src = std::move(src);
  files_.push_back(std::move(entry));
  return static_cast<std::uint32_t>(files_.size() - 1);
}

std::pair<std::uint32_t, std::uint32_t> SourceMap::lookup_line_col(Span span) const {
  const FileEntry* entry = file(span.file_id);
  if (entry == nullptr || entry->line_starts.empty()) {
    return {1, 1};
  }

  const std::uint32_t offset = clamp_offset(span.file_id, span.lo);
  auto it = std::upper_bound(entry->line_starts.begin(), entry->line_starts.end(), offset);
  if (it == entry->line_starts.begin()) {
    return {1, offset + 1};
  }
  --it;
  const std::uint32_t line_index = static_cast<std::uint32_t>(std::distance(entry->line_starts.begin(), it));
  const std::uint32_t line_start = *it;
  return {line_index + 1, (offset - line_start) + 1};
}

std::string_view SourceMap::snippet(Span span) const {
  const FileEntry* entry = file(span.file_id);
  if (entry == nullptr) {
    return {};
  }
  const std::uint32_t lo = clamp_offset(span.file_id, span.lo);
  const std::uint32_t hi = clamp_offset(span.file_id, span.hi);
  if (hi <= lo) {
    return {};
  }
  return std::string_view(entry->src).substr(lo, hi - lo);
}

std::string_view SourceMap::line_text(std::uint32_t file_id, std::uint32_t line) const {
  const FileEntry* entry = file(file_id);
  if (entry == nullptr || line == 0 || line > entry->line_starts.size()) {
    return {};
  }
  const std::uint32_t idx = line - 1;
  const std::uint32_t lo = entry->line_starts[idx];
  std::uint32_t hi = static_cast<std::uint32_t>(entry->src.size());
  if (idx + 1 < entry->line_starts.size()) {
    hi = entry->line_starts[idx + 1];
  }
  while (hi > lo && (entry->src[hi - 1] == '\n' || entry->src[hi - 1] == '\r')) {
    --hi;
  }
  return std::string_view(entry->src).substr(lo, hi - lo);
}

}  // namespace thagc::syntax

