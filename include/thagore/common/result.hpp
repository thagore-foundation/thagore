#pragma once

#include <expected>

namespace thagore {

template <typename T, typename E>
using Result = std::expected<T, E>;

} // namespace thagore
