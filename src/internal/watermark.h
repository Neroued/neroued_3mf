// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file watermark.h
/// \brief L1 watermark: encode/decode arbitrary payload into triangle index rotations.
///
/// Frame format (per-bit, 1 bit/triangle):
///   magic(4B "N3MW") | flags(1B) | length(2B LE) | payload(NB) | crc32(4B)
/// Overhead = 11 bytes. Payload encrypted via XOR with HMAC-SHA256 counter-mode keystream.

#include "sha256.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace neroued_3mf::detail {

inline constexpr uint8_t kWmMagic[4] = {0x4E, 0x33, 0x4D, 0x57}; // "N3MW"
inline constexpr std::size_t kWmOverhead = 11; // magic(4) + flags(1) + length(2) + crc32(4)
inline constexpr uint8_t kWmFlagTruncated = 0x01;

/// Returns r in {0,1,2} such that applying kTriRotation[r] to (a,b,c)
/// yields the lexicographically smallest cyclic permutation.
/// Used by the encoder (opc.h) and decoder (watermark_detect.cpp).
inline uint8_t CanonicalRotation(uint32_t a, uint32_t b, uint32_t c) {
    auto lt = [](uint32_t x0, uint32_t y0, uint32_t z0, uint32_t x1, uint32_t y1,
                 uint32_t z1) -> bool {
        if (x0 != x1) return x0 < x1;
        if (y0 != y1) return y0 < y1;
        return z0 < z1;
    };
    // rot 0: (a,b,c), rot 1: (b,c,a), rot 2: (c,a,b)
    if (lt(b, c, a, a, b, c)) { return lt(c, a, b, b, c, a) ? 2 : 1; }
    return lt(c, a, b, a, b, c) ? 2 : 0;
}

namespace watermark_impl {

inline uint32_t FrameCrc32(const uint8_t *data, std::size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) { crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u)); }
    }
    return crc ^ 0xFFFFFFFFu;
}

inline uint8_t EncodeRepetition(int rep) {
    switch (rep) {
    case 3:
        return 1;
    case 5:
        return 2;
    default:
        return 0; // 1x
    }
}

inline int DecodeRepetition(uint8_t bits) {
    switch (bits & 0x03) {
    case 1:
        return 3;
    case 2:
        return 5;
    default:
        return 1;
    }
}

/// HMAC-SHA256 counter-mode keystream. Returns one bit (0/1) per triangle.
inline std::vector<uint8_t> GenerateKeystream(const std::vector<uint8_t> &key,
                                              std::size_t num_bits) {
    if (key.empty()) { return std::vector<uint8_t>(num_bits, 0); }

    std::vector<uint8_t> bits;
    bits.reserve(num_bits);

    static constexpr char kLabel[] = "n3mf-watermark-v1";
    constexpr std::size_t kLabelLen = sizeof(kLabel) - 1;

    uint32_t counter = 0;
    while (bits.size() < num_bits) {
        uint8_t msg[kLabelLen + 4];
        std::memcpy(msg, kLabel, kLabelLen);
        msg[kLabelLen] = static_cast<uint8_t>(counter & 0xffu);
        msg[kLabelLen + 1] = static_cast<uint8_t>((counter >> 8) & 0xffu);
        msg[kLabelLen + 2] = static_cast<uint8_t>((counter >> 16) & 0xffu);
        msg[kLabelLen + 3] = static_cast<uint8_t>((counter >> 24) & 0xffu);

        auto hash = HmacSHA256(key.data(), key.size(), msg, sizeof(msg));
        for (std::size_t i = 0; i < 32 && bits.size() < num_bits; ++i) {
            for (int b = 7; b >= 0 && bits.size() < num_bits; --b) {
                bits.push_back((hash[i] >> b) & 1u);
            }
        }
        ++counter;
    }
    return bits;
}

/// Build frame bytes and convert to bit stream.
inline std::vector<uint8_t> EncodeFrame(const uint8_t *payload, std::size_t payload_len,
                                        bool truncated, int actual_rep) {
    std::vector<uint8_t> frame;
    frame.reserve(kWmOverhead + payload_len);

    frame.insert(frame.end(), kWmMagic, kWmMagic + 4);

    uint8_t flags = 0;
    if (truncated) { flags |= kWmFlagTruncated; }
    flags |= static_cast<uint8_t>(EncodeRepetition(actual_rep) << 1);
    frame.push_back(flags);

    frame.push_back(static_cast<uint8_t>(payload_len & 0xffu));
    frame.push_back(static_cast<uint8_t>((payload_len >> 8) & 0xffu));

    frame.insert(frame.end(), payload, payload + payload_len);

    uint32_t crc = FrameCrc32(payload, payload_len);
    frame.push_back(static_cast<uint8_t>(crc & 0xffu));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xffu));
    frame.push_back(static_cast<uint8_t>((crc >> 16) & 0xffu));
    frame.push_back(static_cast<uint8_t>((crc >> 24) & 0xffu));

    std::vector<uint8_t> bits;
    bits.reserve(frame.size() * 8);
    for (uint8_t byte : frame) {
        for (int b = 7; b >= 0; --b) { bits.push_back((byte >> b) & 1u); }
    }
    return bits;
}

/// Decode bits (with majority voting) back to bytes. Returns frame bytes.
inline std::vector<uint8_t> DecodeBitsToBytes(const std::vector<uint8_t> &raw_bits,
                                              std::size_t byte_count, int rep) {
    std::vector<uint8_t> bytes(byte_count, 0);
    for (std::size_t i = 0; i < byte_count; ++i) {
        for (int b = 7; b >= 0; --b) {
            std::size_t bit_idx = i * 8 + static_cast<std::size_t>(7 - b);
            int votes = 0;
            for (int r = 0; r < rep; ++r) {
                std::size_t tri =
                    bit_idx * static_cast<std::size_t>(rep) + static_cast<std::size_t>(r);
                if (tri < raw_bits.size()) { votes += raw_bits[tri] ? 1 : -1; }
            }
            if (votes > 0) { bytes[i] |= (1u << b); }
        }
    }
    return bytes;
}

} // namespace watermark_impl

/// Build per-triangle rotation table (0 or 1 per entry).
/// Returns empty vector if watermark is disabled or triangles are insufficient.
inline std::vector<uint8_t> BuildRotationTable(const std::vector<uint8_t> &payload,
                                               const std::vector<uint8_t> &key, int configured_rep,
                                               std::size_t total_triangles) {
    if (payload.empty()) { return {}; }
    if (configured_rep != 1 && configured_rep != 3 && configured_rep != 5) { configured_rep = 3; }

    std::size_t available_bits = total_triangles;
    std::size_t full_frame_bits = (kWmOverhead + payload.size()) * 8;

    int actual_rep = configured_rep;
    std::size_t actual_payload_len = payload.size();
    bool truncated = false;

    if (full_frame_bits * static_cast<std::size_t>(actual_rep) <= available_bits) {
        // fits with configured repetition
    } else if (full_frame_bits <= available_bits) {
        actual_rep = 1;
    } else if (available_bits >= kWmOverhead * 8) {
        actual_rep = 1;
        actual_payload_len = available_bits / 8 - kWmOverhead;
        truncated = true;
    } else {
        return {};
    }

    auto frame_bits =
        watermark_impl::EncodeFrame(payload.data(), actual_payload_len, truncated, actual_rep);
    auto keystream = watermark_impl::GenerateKeystream(key, total_triangles);

    std::vector<uint8_t> table(total_triangles, 0);
    std::size_t encoded_bits = frame_bits.size() * static_cast<std::size_t>(actual_rep);

    for (std::size_t ti = 0; ti < total_triangles; ++ti) {
        uint8_t data_bit = 0;
        if (ti < encoded_bits) { data_bit = frame_bits[ti / static_cast<std::size_t>(actual_rep)]; }
        table[ti] = data_bit ^ keystream[ti];
    }
    return table;
}

/// Decode payload from observed triangle rotations.
/// Returns decoded payload bytes, or empty on failure (bad magic / CRC mismatch).
/// If \p truncated is non-null, sets it to true when the encoded payload was truncated.
inline std::vector<uint8_t> DecodePayload(const std::vector<uint8_t> &key,
                                          const std::vector<uint8_t> &rotations,
                                          bool *truncated = nullptr) {
    if (truncated) *truncated = false;
    if (rotations.empty()) { return {}; }

    auto keystream = watermark_impl::GenerateKeystream(key, rotations.size());

    std::vector<uint8_t> raw_bits(rotations.size());
    for (std::size_t i = 0; i < rotations.size(); ++i) {
        raw_bits[i] = (rotations[i] & 1u) ^ keystream[i];
    }

    constexpr std::size_t kHeaderBytes = 7; // magic(4) + flags(1) + length(2)

    for (int rep : {1, 3, 5}) {
        if (kHeaderBytes * 8 * static_cast<std::size_t>(rep) > rotations.size()) { continue; }

        auto header = watermark_impl::DecodeBitsToBytes(raw_bits, kHeaderBytes, rep);
        if (std::memcmp(header.data(), kWmMagic, 4) != 0) { continue; }

        uint8_t flags = header[4];
        int stored_rep = watermark_impl::DecodeRepetition(static_cast<uint8_t>(flags >> 1));
        if (stored_rep != rep) { continue; }

        uint16_t length =
            static_cast<uint16_t>(header[5]) | (static_cast<uint16_t>(header[6]) << 8);
        std::size_t frame_size = kWmOverhead + length;
        if (frame_size * 8 * static_cast<std::size_t>(rep) > rotations.size()) { continue; }

        auto frame_bytes = watermark_impl::DecodeBitsToBytes(raw_bits, frame_size, rep);
        const uint8_t *payload_ptr = frame_bytes.data() + kHeaderBytes;

        uint32_t stored_crc =
            static_cast<uint32_t>(frame_bytes[kHeaderBytes + length]) |
            (static_cast<uint32_t>(frame_bytes[kHeaderBytes + length + 1]) << 8) |
            (static_cast<uint32_t>(frame_bytes[kHeaderBytes + length + 2]) << 16) |
            (static_cast<uint32_t>(frame_bytes[kHeaderBytes + length + 3]) << 24);

        if (stored_crc == watermark_impl::FrameCrc32(payload_ptr, length)) {
            if (truncated) *truncated = (flags & kWmFlagTruncated) != 0;
            return {payload_ptr, payload_ptr + length};
        }
    }
    return {};
}

// -- XML triangle scanner ---------------------------------------------------

namespace watermark_impl {

inline bool ParseTriAttrU32(std::string_view tag, std::string_view prefix, uint32_t &out) {
    auto p = tag.find(prefix);
    if (p == std::string_view::npos) return false;
    p += prefix.size();
    uint32_t val = 0;
    bool has_digit = false;
    while (p < tag.size() && tag[p] >= '0' && tag[p] <= '9') {
        val = val * 10 + static_cast<uint32_t>(tag[p] - '0');
        ++p;
        has_digit = true;
    }
    if (!has_digit || p >= tag.size() || tag[p] != '"') return false;
    out = val;
    return true;
}

} // namespace watermark_impl

/// Scan XML for <triangle v1="..." v2="..." v3="..."/> tags.
/// Returns {v1,v2,v3} per triangle in document order.
inline std::vector<std::array<uint32_t, 3>> ScanTrianglesFromXml(std::string_view xml) {
    std::vector<std::array<uint32_t, 3>> result;

    static constexpr std::string_view kTag = "<triangle";
    static constexpr std::string_view kV1 = " v1=\"";
    static constexpr std::string_view kV2 = " v2=\"";
    static constexpr std::string_view kV3 = " v3=\"";

    std::size_t pos = 0;
    while ((pos = xml.find(kTag, pos)) != std::string_view::npos) {
        pos += kTag.size();
        if (pos >= xml.size()) break;

        char next = xml[pos];
        if (next != ' ' && next != '\t' && next != '\n' && next != '\r') continue;

        auto tag_end = xml.find('>', pos);
        if (tag_end == std::string_view::npos) break;

        auto tag = xml.substr(pos, tag_end - pos);
        uint32_t v1 = 0, v2 = 0, v3 = 0;
        if (watermark_impl::ParseTriAttrU32(tag, kV1, v1) &&
            watermark_impl::ParseTriAttrU32(tag, kV2, v2) &&
            watermark_impl::ParseTriAttrU32(tag, kV3, v3)) {
            result.push_back({v1, v2, v3});
        }

        pos = tag_end + 1;
    }

    return result;
}

} // namespace neroued_3mf::detail
