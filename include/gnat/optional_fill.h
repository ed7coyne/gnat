#pragma once

// This file exists to allow this library to easily run both where std::optional
// is present and where it is not.

#if __has_include(<optional> )

#include <optional>

#else

#include "./optional.hpp"

namespace std {
using nonstd::optional;
}

#endif  // has_include
