#pragma once

#include <cstdint>

namespace thagc::syntax {

struct Span {
  std::uint32_t lo = 0;
  std::uint32_t hi = 0;
  std::uint32_t file_id = 0;

  bool valid() const {
    return hi > lo;
  }
};

}  // namespace thagc::syntax

