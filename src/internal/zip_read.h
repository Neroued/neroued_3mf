// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file zip_read.h
/// \brief Minimal ZIP reader for watermark detection. Parses EOCD/CD, extracts entries
///        (store / deflate). Does not handle encryption or multi-disk archives.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#if defined(NEROUED_3MF_HAS_ZLIB)
#include <zlib.h>
#endif

namespace neroued_3mf::detail {

struct ZipEntry {
    std::string path;
    uint16_t compression_method = 0;
    uint16_t flags = 0;
    uint32_t crc32 = 0;
    uint64_t compressed_size = 0;
    uint64_t uncompressed_size = 0;
    uint64_t local_header_offset = 0;
};

namespace zip_read_impl {

inline uint16_t ReadU16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t ReadU32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t ReadU64(const uint8_t *p) {
    return static_cast<uint64_t>(ReadU32(p)) | (static_cast<uint64_t>(ReadU32(p + 4)) << 32);
}

inline void ParseZip64Extra(const uint8_t *extra, const uint8_t *extra_end, ZipEntry &e) {
    while (extra + 4 <= extra_end) {
        uint16_t tag = ReadU16(extra);
        uint16_t data_size = ReadU16(extra + 2);
        if (extra + 4 + data_size > extra_end) break;
        if (tag == 0x0001) {
            const uint8_t *field = extra + 4;
            std::size_t remaining = data_size;
            if (e.uncompressed_size == 0xFFFFFFFFu && remaining >= 8) {
                e.uncompressed_size = ReadU64(field);
                field += 8;
                remaining -= 8;
            }
            if (e.compressed_size == 0xFFFFFFFFu && remaining >= 8) {
                e.compressed_size = ReadU64(field);
                field += 8;
                remaining -= 8;
            }
            if (e.local_header_offset == 0xFFFFFFFFu && remaining >= 8) {
                e.local_header_offset = ReadU64(field);
            }
            return;
        }
        extra += 4 + data_size;
    }
}

} // namespace zip_read_impl

/// Locate EOCD (ZIP32/64), parse Central Directory, return entry list.
/// Returns empty vector on parse failure.
inline std::vector<ZipEntry> FindZipEntries(std::span<const uint8_t> data) {
    using namespace zip_read_impl;

    constexpr std::size_t kMinEocd = 22;
    if (data.size() < kMinEocd) return {};

    const uint8_t *base = data.data();
    std::size_t size = data.size();

    constexpr std::size_t kMaxComment = 65535;
    std::size_t scan_start = (size > kMinEocd + kMaxComment) ? size - kMinEocd - kMaxComment : 0;

    const uint8_t *eocd = nullptr;
    for (std::size_t i = size - kMinEocd;; --i) {
        if (ReadU32(base + i) == 0x06054B50u) {
            eocd = base + i;
            break;
        }
        if (i <= scan_start) break;
    }
    if (!eocd) return {};

    uint64_t cd_offset = ReadU32(eocd + 16);
    uint64_t cd_size = ReadU32(eocd + 12);
    uint64_t entry_count = ReadU16(eocd + 10);

    // ZIP64 EOCD locator sits immediately before the EOCD
    if (static_cast<std::size_t>(eocd - base) >= 20) {
        const uint8_t *locator = eocd - 20;
        if (ReadU32(locator) == 0x07064B50u) {
            uint64_t zip64_eocd_off = ReadU64(locator + 8);
            if (zip64_eocd_off + 56 <= size) {
                const uint8_t *z64 = base + zip64_eocd_off;
                if (ReadU32(z64) == 0x06064B50u) {
                    entry_count = ReadU64(z64 + 32);
                    cd_size = ReadU64(z64 + 40);
                    cd_offset = ReadU64(z64 + 48);
                }
            }
        }
    }

    if (cd_offset + cd_size > size) return {};

    std::vector<ZipEntry> entries;
    entries.reserve(static_cast<std::size_t>(entry_count));

    const uint8_t *p = base + cd_offset;
    const uint8_t *cd_end = p + cd_size;

    for (uint64_t i = 0; i < entry_count && p + 46 <= cd_end; ++i) {
        if (ReadU32(p) != 0x02014B50u) break;

        ZipEntry e;
        e.flags = ReadU16(p + 8);
        e.compression_method = ReadU16(p + 10);
        e.crc32 = ReadU32(p + 16);
        e.compressed_size = ReadU32(p + 20);
        e.uncompressed_size = ReadU32(p + 24);

        uint16_t name_len = ReadU16(p + 28);
        uint16_t extra_len = ReadU16(p + 30);
        uint16_t comment_len = ReadU16(p + 32);
        e.local_header_offset = ReadU32(p + 42);

        if (p + 46 + name_len > cd_end) break;
        e.path.assign(reinterpret_cast<const char *>(p + 46), name_len);

        const uint8_t *extra = p + 46 + name_len;
        const uint8_t *extra_end = extra + extra_len;
        if (extra_end > cd_end) break;
        ParseZip64Extra(extra, extra_end, e);

        entries.push_back(std::move(e));
        p += 46 + name_len + extra_len + comment_len;
    }

    return entries;
}

/// Extract and decompress entry data. Returns nullopt on failure or unsupported compression.
inline std::optional<std::vector<uint8_t>> ExtractEntry(std::span<const uint8_t> data,
                                                        const ZipEntry &entry) {
    using namespace zip_read_impl;

    if (entry.local_header_offset + 30 > data.size()) return std::nullopt;

    const uint8_t *lfh = data.data() + entry.local_header_offset;
    if (ReadU32(lfh) != 0x04034B50u) return std::nullopt;

    uint16_t name_len = ReadU16(lfh + 26);
    uint16_t extra_len = ReadU16(lfh + 28);
    uint64_t payload_offset = entry.local_header_offset + 30 + name_len + extra_len;

    if (payload_offset + entry.compressed_size > data.size()) return std::nullopt;

    const uint8_t *payload = data.data() + payload_offset;

    if (entry.compression_method == 0) {
        return std::vector<uint8_t>(payload, payload + entry.compressed_size);
    }

#if defined(NEROUED_3MF_HAS_ZLIB)
    if (entry.compression_method == 8) {
        constexpr auto kMaxUInt = static_cast<uint64_t>(std::numeric_limits<uInt>::max());
        if (entry.compressed_size > kMaxUInt || entry.uncompressed_size > kMaxUInt)
            return std::nullopt;

        std::vector<uint8_t> out(static_cast<std::size_t>(entry.uncompressed_size));

        z_stream zs{};
        zs.next_in = const_cast<Bytef *>(payload);
        zs.avail_in = static_cast<uInt>(entry.compressed_size);
        zs.next_out = out.data();
        zs.avail_out = static_cast<uInt>(out.size());

        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return std::nullopt;

        int rc = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);

        if (rc != Z_STREAM_END) return std::nullopt;
        out.resize(static_cast<std::size_t>(zs.total_out));
        return out;
    }
#endif

    return std::nullopt;
}

} // namespace neroued_3mf::detail
