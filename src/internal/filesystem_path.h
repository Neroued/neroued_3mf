// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

#include <filesystem>
#include <string>

namespace neroued_3mf::detail {

inline std::filesystem::path FileIoPath(const std::filesystem::path &path) {
#if defined(_WIN32)
    const auto &original = path.native();
    if (original.rfind(L"\\\\?\\", 0) == 0 || original.rfind(L"\\\\.\\", 0) == 0) { return path; }
    auto absolute = std::filesystem::absolute(path).lexically_normal();
    absolute.make_preferred();
    const auto &name = absolute.native();
    if (name.rfind(L"\\\\", 0) == 0) {
        return std::filesystem::path(L"\\\\?\\UNC\\" + name.substr(2));
    }
    return std::filesystem::path(L"\\\\?\\" + name);
#else
    return path;
#endif
}

inline std::string PathText(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

} // namespace neroued_3mf::detail
