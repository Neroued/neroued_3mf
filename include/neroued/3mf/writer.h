// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file writer.h
/// \brief 3MF writing API -- serialize a Document to ZIP package.

#include "document.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <vector>

namespace neroued_3mf {

struct WriteOptions {
    enum class Compression { Store, Deflate, Auto } compression = Compression::Auto;
    std::size_t compression_threshold = 16 * 1024;
    int deflate_level = 1;
    /// Use fixed zero timestamps for reproducible output.
    bool deterministic = true;
    /// Omit XML indentation to reduce output size (~15-20% smaller).
    bool compact_xml = false;
    /// Significant digits for vertex coordinates (1-9). Default 9 = full float32 precision.
    /// Lower values reduce XML size at the cost of geometric precision.
    /// Transform matrices always use full precision regardless of this setting.
    int vertex_precision = 9;
};

/// Write a Document to an in-memory buffer.
std::vector<uint8_t> WriteToBuffer(const Document &doc, const WriteOptions &opts = {});

/// Write a Document directly to a file (atomic write via temp file + rename).
void WriteToFile(const std::filesystem::path &path, const Document &doc,
                 const WriteOptions &opts = {});

/// Write a Document to an arbitrary output stream.
void WriteToStream(std::ostream &out, const Document &doc, const WriteOptions &opts = {});

} // namespace neroued_3mf
