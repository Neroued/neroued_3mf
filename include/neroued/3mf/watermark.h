// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file watermark.h
/// \brief Watermark detection API -- detect L2 library fingerprint and decode L1 payload
///        from a 3MF buffer without needing the original mesh data.

#include <cstdint>
#include <span>
#include <vector>

namespace neroued_3mf {

/// Result of watermark detection on a 3MF buffer.
struct WatermarkResult {
    bool has_l2_signature = false;  ///< ZIP extra field library fingerprint detected.
    bool has_l1_payload = false;    ///< L1 payload successfully decoded.
    bool payload_truncated = false; ///< Payload was truncated during encoding (partial data).
    std::vector<uint8_t> payload;   ///< Decoded payload bytes (empty if has_l1_payload is false).
};

/// Detect watermarks in a 3MF buffer.
///
/// Performs both L2 signature detection (always) and L1 payload decoding (when key is provided).
/// L1 decoding extracts triangle vertex rotations from model XML entries and reverses the
/// canonical-rotation encoding to recover the embedded payload.
///
/// \param data  Complete 3MF file content (ZIP archive).
/// \param key   Encryption key used during encoding. Empty = attempt unencrypted decoding.
/// \return Detection result with L2 status and decoded L1 payload (if any).
WatermarkResult DetectWatermark(std::span<const uint8_t> data,
                                const std::vector<uint8_t> &key = {});

/// Fast L2-only detection: scan for the 12-byte library fingerprint in ZIP local headers.
/// Pure binary scan, O(n), no XML parsing.
bool HasL2Signature(std::span<const uint8_t> data);

} // namespace neroued_3mf
