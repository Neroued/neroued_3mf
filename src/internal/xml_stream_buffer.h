// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

#include "neroued/3mf/error.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace neroued_3mf::detail {

/// Fixed-size output buffer that flushes to a sink callback, avoiding per-element heap allocations.
/// All numeric formatting (float, uint32) writes directly into the internal buffer.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // structure padded due to alignas — intentional
#endif
template <typename Sink> class XmlStreamBuffer {
  public:
    static constexpr std::size_t kCapacity = 65536;

    explicit XmlStreamBuffer(Sink &sink) : sink_(sink) {}

    ~XmlStreamBuffer() {
        try {
            Flush();
        } catch (...) {}
    }

    XmlStreamBuffer(const XmlStreamBuffer &) = delete;
    XmlStreamBuffer &operator=(const XmlStreamBuffer &) = delete;

    void Append(std::string_view sv) {
        const char *data = sv.data();
        std::size_t len = sv.size();
        while (len > 0) {
            std::size_t avail = kCapacity - pos_;
            std::size_t chunk = (len < avail) ? len : avail;
            std::memcpy(buf_ + pos_, data, chunk);
            pos_ += chunk;
            if (pos_ == kCapacity) { Flush(); }
            data += chunk;
            len -= chunk;
        }
    }

    void AppendFloat(float value) {
        if (!std::isfinite(value)) { throw InputError("Non-finite float in 3MF serialization"); }
        EnsureSpace(64);
        auto result =
            std::to_chars(buf_ + pos_, buf_ + kCapacity, value, std::chars_format::general,
                          std::numeric_limits<float>::max_digits10);
        if (result.ec != std::errc()) { throw IOError("Failed to format float value"); }
        pos_ = static_cast<std::size_t>(result.ptr - buf_);
    }

    void AppendUint32(uint32_t value) {
        EnsureSpace(16);
        auto result = std::to_chars(buf_ + pos_, buf_ + kCapacity, value);
        pos_ = static_cast<std::size_t>(result.ptr - buf_);
    }

    void Flush() {
        if (pos_ > 0) {
            sink_(std::string_view(buf_, pos_));
            pos_ = 0;
        }
    }

  private:
    void EnsureSpace(std::size_t needed) {
        if (pos_ + needed > kCapacity) { Flush(); }
    }

    Sink &sink_;
    alignas(64) char buf_[kCapacity];
    std::size_t pos_ = 0;
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace neroued_3mf::detail
