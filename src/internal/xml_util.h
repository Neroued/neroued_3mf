// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

#include "neroued/3mf/error.h"
#include "neroued/3mf/types.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <string>

namespace neroued_3mf::detail {

inline std::string EscapeXml(const std::string &s) {
    std::string out;
    out.reserve(s.size() + s.size() / 4);
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

inline std::string FormatFloat(float value) {
    if (!std::isfinite(value)) { throw InputError("Non-finite float in 3MF serialization"); }
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
                                std::numeric_limits<float>::max_digits10);
    if (result.ec != std::errc()) { throw IOError("Failed to format float value"); }
    return std::string(buffer, result.ptr);
}

inline std::string SerializeTransform(const Transform &transform) {
    std::string out;
    out.reserve(12 * 12);
    for (std::size_t i = 0; i < transform.m.size(); ++i) {
        if (i > 0) { out.push_back(' '); }
        out += FormatFloat(transform.m[i]);
    }
    return out;
}

} // namespace neroued_3mf::detail
