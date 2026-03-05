#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "thagc/frontend/span.hpp"

namespace thagc::syntax {

class SourceMap {
 public:
  std::uint32_t add_file(std::string path, std::string src);
  std::pair<std::uint32_t, std::uint32_t> lookup_line_col(Span span) const;
  std::string_view snippet(Span span) const;
  std::string_view line_text(std::uint32_t file_id, std::uint32_t line) const;

 private:
  struct FileEntry {
    std::string path;
    std::string src;
    std::vector<std::uint32_t> line_starts;
  };

  static std::vector<std::uint32_t> compute_line_starts(const std::string& src);
  std::uint32_t clamp_offset(std::uint32_t file_id, std::uint32_t offset) const;
  const FileEntry* file(std::uint32_t file_id) const;

  std::vector<FileEntry> files_;
};

}  // namespace thagc::syntax

