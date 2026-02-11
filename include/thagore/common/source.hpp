#pragma once

#include <cstddef>
#include <string>

namespace thagore {

struct SourceLocation {
  std::size_t line {1};
  std::size_t column {1};
  std::size_t offset {0};
};

struct SourceSpan {
  SourceLocation begin {};
  SourceLocation end {};
  std::string file {};
};

inline auto mergeSpan(const SourceSpan &a, const SourceSpan &b) -> SourceSpan {
  return SourceSpan {
    .begin = a.begin,
    .end = b.end,
    .file = a.file,
  };
}

} // namespace thagore
