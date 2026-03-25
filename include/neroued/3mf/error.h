// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

#include <stdexcept>

namespace neroued_3mf {

class InputError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class IOError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// Malformed 3MF / XML content (used by Reader in Phase 2).
class FormatError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

} // namespace neroued_3mf
