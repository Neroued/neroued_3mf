// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

#include <cstddef>

#if defined(NEROUED_3MF_HAS_OPENMP)
#include <omp.h>
#endif

namespace neroued_3mf::detail {

inline constexpr std::size_t kOmpBBoxThreshold = 100'000;
inline constexpr std::size_t kOmpValidateThreshold = 100'000;
inline constexpr std::size_t kOmpMeshXmlThreshold = 50'000;

} // namespace neroued_3mf::detail
