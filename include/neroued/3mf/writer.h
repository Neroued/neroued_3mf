// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file writer.h
/// \brief 3MF writing API -- serialize a Document to ZIP package.

#include "document.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
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
};

/// Write a Document to an in-memory buffer.
std::vector<uint8_t> WriteToBuffer(const Document &doc, const WriteOptions &opts = {});

/// Write a Document directly to a file (atomic write via temp file + rename).
void WriteToFile(const std::string &path, const Document &doc, const WriteOptions &opts = {});

/// Write a Document to an arbitrary output stream.
void WriteToStream(std::ostream &out, const Document &doc, const WriteOptions &opts = {});

} // namespace neroued_3mf
