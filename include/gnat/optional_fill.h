#pragma once

// This file exists to allow this library to easily run both where std::optional
// is present and where it is not.

#include <optional.hpp>

#if !optional_USES_STD_OPTIONAL

namespace std {
  using nonstd::optional;
}

#endif // optional_USES_STD_OPTIONAL
