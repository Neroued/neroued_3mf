// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file materials.h
/// \brief Material resource types for 3MF basematerials.

#include "types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace neroued_3mf {

struct BaseMaterial {
    std::string name;
    Color display_color;
};

struct BaseMaterialGroup {
    uint32_t id = 0;
    std::vector<BaseMaterial> materials;
};

} // namespace neroued_3mf
