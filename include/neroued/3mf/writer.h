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

/// Configuration for L1 triangle-rotation watermark.
/// When payload is non-empty, the library encodes it into the cyclic ordering of triangle indices
/// (geometrically equivalent, invisible to 3MF readers). The key encrypts the payload via
/// XOR with an HMAC-SHA256 counter-mode keystream; without the key the rotations are
/// statistically indistinguishable from random.
struct WatermarkConfig {
    std::vector<uint8_t> payload; ///< Data to embed. Empty = no L1 watermark.
    std::vector<uint8_t> key;     ///< Encryption key. Empty = unencrypted (payload readable).
    int repetition = 3; ///< Redundancy factor: 1, 3, or 5 (majority-vote error correction).
};

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
    /// L1 watermark configuration. Default (empty payload) = disabled.
    WatermarkConfig watermark;
};

/// Write a Document to an in-memory buffer.
std::vector<uint8_t> WriteToBuffer(const Document &doc, const WriteOptions &opts = {});

/// Write a Document directly to a file (atomic write via temp file + rename).
void WriteToFile(const std::filesystem::path &path, const Document &doc,
                 const WriteOptions &opts = {});

/// Write a Document to an arbitrary output stream.
void WriteToStream(std::ostream &out, const Document &doc, const WriteOptions &opts = {});

} // namespace neroued_3mf
