// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace neroued_3mf::detail {

struct OpcPart {
    std::string path_in_zip;
    std::string content_type;
    std::vector<uint8_t> data;
};

struct OpcRelationship {
    std::string id;
    std::string type;
    std::string target;
};

} // namespace neroued_3mf::detail
