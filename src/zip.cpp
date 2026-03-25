// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#include "zip.h"

#include "neroued/3mf/error.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <ostream>
#include <span>
#include <string>
#include <vector>

#if defined(NEROUED_3MF_HAS_ZLIB)
#include <zlib.h>
#endif

namespace neroued_3mf::detail {

namespace {

constexpr uint32_t kLocalFileHeaderSignature = 0x04034B50u;
constexpr uint32_t kCentralDirHeaderSignature = 0x02014B50u;
constexpr uint32_t kEndOfCentralDirSignature = 0x06054B50u;
constexpr uint32_t kDataDescriptorSignature = 0x08074B50u;
constexpr uint32_t kZip64EocdSignature = 0x06064B50u;
constexpr uint32_t kZip64LocatorSignature = 0x07064B50u;
constexpr uint16_t kZipVersionNeeded = 20u;
constexpr uint16_t kZipVersionMadeBy = 20u;
constexpr uint16_t kZipVersionZip64 = 45u;
constexpr uint16_t kCompressionMethodStore = 0u;
constexpr uint16_t kCompressionMethodDeflate = 8u;
constexpr uint16_t kFlagDataDescriptor = 0x0008u;

struct CentralDirEntry {
    std::string file_name;
    uint16_t compression_method = 0;
    uint16_t general_flags = 0;
    uint32_t crc32 = 0;
    uint64_t compressed_size = 0;
    uint64_t uncompressed_size = 0;
    uint64_t local_header_offset = 0;
    uint16_t mod_time = 0;
    uint16_t mod_date = 0;

    bool NeedsZip64() const {
        return compressed_size > 0xFFFFFFFFu || uncompressed_size > 0xFFFFFFFFu ||
               local_header_offset > 0xFFFFFFFFu;
    }
};

std::pair<uint16_t, uint16_t> MakeDosTimestamp(bool deterministic) {
    if (deterministic) { return {0, 0}; }
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    int year = tm.tm_year + 1900;
    if (year < 1980) { year = 1980; }
    uint16_t dos_time = static_cast<uint16_t>(((tm.tm_hour & 0x1f) << 11) |
                                              ((tm.tm_min & 0x3f) << 5) | ((tm.tm_sec / 2) & 0x1f));
    uint16_t dos_date =
        static_cast<uint16_t>(((year - 1980) << 9) | ((tm.tm_mon + 1) << 5) | (tm.tm_mday & 0x1f));
    return {dos_time, dos_date};
}

// -- CRC32 ------------------------------------------------------------------

#if defined(NEROUED_3MF_HAS_ZLIB)

uint32_t ComputeCrc32(const void *data, std::size_t len) {
    auto *p = static_cast<const Bytef *>(data);
    uLong crc = ::crc32(0L, Z_NULL, 0);
    constexpr std::size_t kMaxChunk = static_cast<std::size_t>(std::numeric_limits<uInt>::max());
    while (len > 0) {
        uInt chunk = static_cast<uInt>((len < kMaxChunk) ? len : kMaxChunk);
        crc = ::crc32(crc, p, chunk);
        p += chunk;
        len -= chunk;
    }
    return static_cast<uint32_t>(crc);
}

#else

struct Crc32Tables {
    uint32_t tab[4][256];
};

const Crc32Tables &GetCrc32Tables() {
    static const Crc32Tables tables = []() {
        Crc32Tables t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j) { c = (c & 1u) ? (0xEDB88320u ^ (c >> 1u)) : (c >> 1u); }
            t.tab[0][i] = c;
        }
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = t.tab[0][i];
            for (int k = 1; k < 4; ++k) {
                c = t.tab[0][c & 0xFFu] ^ (c >> 8u);
                t.tab[k][i] = c;
            }
        }
        return t;
    }();
    return tables;
}

uint32_t Crc32Update(uint32_t crc, const void *data, std::size_t len) {
    const auto &t = GetCrc32Tables();
    auto *p = static_cast<const uint8_t *>(data);

    while (len >= 4) {
        crc ^= static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        crc = t.tab[3][crc & 0xFFu] ^ t.tab[2][(crc >> 8) & 0xFFu] ^ t.tab[1][(crc >> 16) & 0xFFu] ^
              t.tab[0][(crc >> 24) & 0xFFu];
        p += 4;
        len -= 4;
    }

    while (len > 0) {
        crc = t.tab[0][(crc ^ *p++) & 0xFFu] ^ (crc >> 8u);
        --len;
    }
    return crc;
}

uint32_t ComputeCrc32(const void *data, std::size_t len) {
    return Crc32Update(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

#endif

// -- Deflate helpers --------------------------------------------------------

bool ShouldTryDeflate(std::size_t data_size, const WriteOptions &options) {
    switch (options.compression) {
    case WriteOptions::Compression::Store:
        return false;
    case WriteOptions::Compression::Deflate:
        return true;
    case WriteOptions::Compression::Auto:
        return data_size >= options.compression_threshold;
    }
    return false;
}

#if defined(NEROUED_3MF_HAS_ZLIB)
bool TryDeflateRaw(const void *input_data, std::size_t input_size, int level,
                   std::vector<uint8_t> &output) {
    if (input_size == 0) {
        output.clear();
        return true;
    }
    if (input_size > static_cast<std::size_t>(std::numeric_limits<uInt>::max())) { return false; }

    z_stream stream{};
    if (deflateInit2(&stream, level, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return false;
    }

    uLong bound = compressBound(static_cast<uLong>(input_size));
    output.assign(static_cast<std::size_t>(bound), 0u);

    stream.next_in = static_cast<Bytef *>(const_cast<void *>(input_data));
    stream.avail_in = static_cast<uInt>(input_size);
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(output.size());

    if (deflate(&stream, Z_FINISH) != Z_STREAM_END) {
        deflateEnd(&stream);
        return false;
    }
    if (deflateEnd(&stream) != Z_OK) { return false; }
    output.resize(static_cast<std::size_t>(stream.total_out));
    return true;
}
#endif

} // namespace

// -- StreamingZipWriter::Impl -----------------------------------------------

struct StreamingZipWriter::Impl {
    enum class SinkKind { Buffer, File, Stream };
    SinkKind sink_kind;
    std::vector<uint8_t> *buf_sink = nullptr;
    std::ofstream file_sink;
    std::ostream *stream_sink = nullptr;
    std::size_t sink_size = 0;

    WriteOptions options;
    std::vector<CentralDirEntry> central_entries;
    uint16_t dos_time = 0;
    uint16_t dos_date = 0;

    bool in_stream_entry = false;
    bool stream_is_deflating = false;
    std::string stream_filename;
    uint64_t stream_local_header_offset = 0;
    std::size_t stream_data_start = 0;
    uint64_t stream_uncompressed = 0;

#if defined(NEROUED_3MF_HAS_ZLIB)
    uLong stream_crc = 0;
    z_stream zstream{};
    bool zstream_active = false;
#else
    uint32_t stream_crc = 0xFFFFFFFFu;
#endif

    Impl(std::vector<uint8_t> &output, WriteOptions opts)
        : sink_kind(SinkKind::Buffer), buf_sink(&output), options(std::move(opts)) {
        auto [t, d] = MakeDosTimestamp(options.deterministic);
        dos_time = t;
        dos_date = d;
    }

    Impl(const std::filesystem::path &path, WriteOptions opts)
        : sink_kind(SinkKind::File), file_sink(path, std::ios::binary | std::ios::trunc),
          options(std::move(opts)) {
        if (!file_sink.is_open()) {
            throw IOError("Cannot open file for writing: " + path.string());
        }
        auto [t, d] = MakeDosTimestamp(options.deterministic);
        dos_time = t;
        dos_date = d;
    }

    Impl(std::ostream &out, WriteOptions opts)
        : sink_kind(SinkKind::Stream), stream_sink(&out), options(std::move(opts)) {
        auto [t, d] = MakeDosTimestamp(options.deterministic);
        dos_time = t;
        dos_date = d;
    }

    ~Impl() {
#if defined(NEROUED_3MF_HAS_ZLIB)
        if (zstream_active) { deflateEnd(&zstream); }
#endif
    }

    // -- Sink operations (no virtual dispatch) --

    void SinkAppend(const void *data, std::size_t len) {
        switch (sink_kind) {
        case SinkKind::Buffer: {
            auto *p = static_cast<const uint8_t *>(data);
            buf_sink->insert(buf_sink->end(), p, p + len);
            break;
        }
        case SinkKind::File:
            file_sink.write(static_cast<const char *>(data), static_cast<std::streamsize>(len));
            if (!file_sink.good()) { throw IOError("Failed to write to 3MF file"); }
            break;
        case SinkKind::Stream:
            stream_sink->write(static_cast<const char *>(data), static_cast<std::streamsize>(len));
            if (!stream_sink->good()) { throw IOError("Failed to write to 3MF output stream"); }
            break;
        }
        sink_size += len;
    }

    std::size_t SinkSize() const { return sink_size; }

    void SinkAppendU16(uint16_t v) {
        uint8_t b[2] = {static_cast<uint8_t>(v & 0xffu), static_cast<uint8_t>((v >> 8) & 0xffu)};
        SinkAppend(b, 2);
    }

    void SinkAppendU32(uint32_t v) {
        uint8_t b[4] = {static_cast<uint8_t>(v & 0xffu), static_cast<uint8_t>((v >> 8) & 0xffu),
                        static_cast<uint8_t>((v >> 16) & 0xffu),
                        static_cast<uint8_t>((v >> 24) & 0xffu)};
        SinkAppend(b, 4);
    }

    void SinkAppendU64(uint64_t v) {
        uint8_t b[8] = {
            static_cast<uint8_t>(v & 0xffu),         static_cast<uint8_t>((v >> 8) & 0xffu),
            static_cast<uint8_t>((v >> 16) & 0xffu), static_cast<uint8_t>((v >> 24) & 0xffu),
            static_cast<uint8_t>((v >> 32) & 0xffu), static_cast<uint8_t>((v >> 40) & 0xffu),
            static_cast<uint8_t>((v >> 48) & 0xffu), static_cast<uint8_t>((v >> 56) & 0xffu)};
        SinkAppend(b, 8);
    }

    void SinkAppendBytes(const void *data, std::size_t len) { SinkAppend(data, len); }

    // -- CRC --

    void InitStreamCrc() {
#if defined(NEROUED_3MF_HAS_ZLIB)
        stream_crc = ::crc32(0L, Z_NULL, 0);
#else
        stream_crc = 0xFFFFFFFFu;
#endif
    }

    void UpdateStreamCrc(const void *data, std::size_t len) {
#if defined(NEROUED_3MF_HAS_ZLIB)
        auto *p = static_cast<const Bytef *>(data);
        std::size_t remaining = len;
        constexpr std::size_t kMax = static_cast<std::size_t>(std::numeric_limits<uInt>::max());
        while (remaining > 0) {
            uInt chunk = static_cast<uInt>((remaining < kMax) ? remaining : kMax);
            stream_crc = ::crc32(stream_crc, p, chunk);
            p += chunk;
            remaining -= chunk;
        }
#else
        stream_crc = Crc32Update(stream_crc, data, len);
#endif
    }

    uint32_t FinalStreamCrc() {
#if defined(NEROUED_3MF_HAS_ZLIB)
        return static_cast<uint32_t>(stream_crc);
#else
        return stream_crc ^ 0xFFFFFFFFu;
#endif
    }

    // -- Deflate --

    void FlushDeflate([[maybe_unused]] int flush) {
#if defined(NEROUED_3MF_HAS_ZLIB)
        uint8_t buf[16384];
        int rc;
        do {
            zstream.next_out = buf;
            zstream.avail_out = sizeof(buf);
            rc = deflate(&zstream, flush);
            if (rc == Z_STREAM_ERROR) { throw IOError("zlib deflate stream error"); }
            std::size_t have = sizeof(buf) - zstream.avail_out;
            if (have > 0) { SinkAppend(buf, have); }
        } while (zstream.avail_out == 0);
#endif
    }

    // -- Header writing --

    void WriteLocalHeader(const std::string &name, uint16_t compression_method, uint16_t flags,
                          uint32_t crc, uint32_t compressed_size, uint32_t uncompressed_size) {
        SinkAppendU32(kLocalFileHeaderSignature);
        SinkAppendU16(kZipVersionNeeded);
        SinkAppendU16(flags);
        SinkAppendU16(compression_method);
        SinkAppendU16(dos_time);
        SinkAppendU16(dos_date);
        SinkAppendU32(crc);
        SinkAppendU32(compressed_size);
        SinkAppendU32(uncompressed_size);
        SinkAppendU16(static_cast<uint16_t>(name.size()));
        SinkAppendU16(0u);
        SinkAppendBytes(name.data(), name.size());
    }

    void WriteDataDescriptor(uint32_t crc, uint64_t compressed, uint64_t uncompressed) {
        SinkAppendU32(kDataDescriptorSignature);
        SinkAppendU32(crc);
        SinkAppendU32(static_cast<uint32_t>(compressed));
        SinkAppendU32(static_cast<uint32_t>(uncompressed));
    }

    void WriteCentralDirectory() {
        uint64_t central_dir_offset = static_cast<uint64_t>(sink_size);
        bool any_zip64 = false;

        for (const auto &entry : central_entries) {
            bool zip64 = entry.NeedsZip64();
            if (zip64) { any_zip64 = true; }

            uint16_t extra_len = zip64 ? 28u : 0u;
            uint16_t ver_needed = zip64 ? kZipVersionZip64 : kZipVersionNeeded;
            uint16_t ver_made_by = zip64 ? kZipVersionZip64 : kZipVersionMadeBy;

            SinkAppendU32(kCentralDirHeaderSignature);
            SinkAppendU16(ver_made_by);
            SinkAppendU16(ver_needed);
            SinkAppendU16(entry.general_flags);
            SinkAppendU16(entry.compression_method);
            SinkAppendU16(entry.mod_time);
            SinkAppendU16(entry.mod_date);
            SinkAppendU32(entry.crc32);
            SinkAppendU32(zip64 ? 0xFFFFFFFFu : static_cast<uint32_t>(entry.compressed_size));
            SinkAppendU32(zip64 ? 0xFFFFFFFFu : static_cast<uint32_t>(entry.uncompressed_size));
            SinkAppendU16(static_cast<uint16_t>(entry.file_name.size()));
            SinkAppendU16(extra_len);
            SinkAppendU16(0u);
            SinkAppendU16(0u);
            SinkAppendU16(0u);
            SinkAppendU32(0u);
            SinkAppendU32(zip64 ? 0xFFFFFFFFu : static_cast<uint32_t>(entry.local_header_offset));
            SinkAppendBytes(entry.file_name.data(), entry.file_name.size());

            if (zip64) {
                SinkAppendU16(0x0001u);
                SinkAppendU16(24u);
                SinkAppendU64(entry.uncompressed_size);
                SinkAppendU64(entry.compressed_size);
                SinkAppendU64(entry.local_header_offset);
            }
        }

        uint64_t central_dir_size = static_cast<uint64_t>(sink_size) - central_dir_offset;
        uint64_t entry_count = central_entries.size();

        bool need_zip64_eocd = any_zip64 || entry_count > 0xFFFFu ||
                               central_dir_offset > 0xFFFFFFFFu || central_dir_size > 0xFFFFFFFFu;

        if (need_zip64_eocd) {
            uint64_t zip64_eocd_offset = static_cast<uint64_t>(sink_size);

            SinkAppendU32(kZip64EocdSignature);
            SinkAppendU64(44u);
            SinkAppendU16(kZipVersionZip64);
            SinkAppendU16(kZipVersionZip64);
            SinkAppendU32(0u);
            SinkAppendU32(0u);
            SinkAppendU64(entry_count);
            SinkAppendU64(entry_count);
            SinkAppendU64(central_dir_size);
            SinkAppendU64(central_dir_offset);

            SinkAppendU32(kZip64LocatorSignature);
            SinkAppendU32(0u);
            SinkAppendU64(zip64_eocd_offset);
            SinkAppendU32(1u);
        }

        SinkAppendU32(kEndOfCentralDirSignature);
        SinkAppendU16(0u);
        SinkAppendU16(0u);
        SinkAppendU16(entry_count > 0xFFFFu ? static_cast<uint16_t>(0xFFFFu)
                                            : static_cast<uint16_t>(entry_count));
        SinkAppendU16(entry_count > 0xFFFFu ? static_cast<uint16_t>(0xFFFFu)
                                            : static_cast<uint16_t>(entry_count));
        SinkAppendU32(central_dir_size > 0xFFFFFFFFu ? 0xFFFFFFFFu
                                                     : static_cast<uint32_t>(central_dir_size));
        SinkAppendU32(central_dir_offset > 0xFFFFFFFFu ? 0xFFFFFFFFu
                                                       : static_cast<uint32_t>(central_dir_offset));
        SinkAppendU16(0u);
    }
};

// -- StreamingZipWriter public methods --------------------------------------

StreamingZipWriter::StreamingZipWriter(std::vector<uint8_t> &output, const WriteOptions &options)
    : impl_(std::make_unique<Impl>(output, options)) {}

StreamingZipWriter::StreamingZipWriter(const std::filesystem::path &file_path,
                                       const WriteOptions &options)
    : impl_(std::make_unique<Impl>(file_path, options)) {}

StreamingZipWriter::StreamingZipWriter(std::ostream &output, const WriteOptions &options)
    : impl_(std::make_unique<Impl>(output, options)) {}

StreamingZipWriter::~StreamingZipWriter() = default;

void StreamingZipWriter::WriteWholeEntry(const std::string &path, std::span<const uint8_t> data) {
    if (impl_->in_stream_entry) { throw InputError("Cannot write whole entry while streaming"); }
    if (path.size() > static_cast<std::size_t>(std::numeric_limits<uint16_t>::max())) {
        throw InputError("ZIP entry file name too long: " + path);
    }

    uint16_t compression_method = kCompressionMethodStore;
    const uint8_t *payload_data = data.data();
    std::size_t payload_size = data.size();
    [[maybe_unused]] std::vector<uint8_t> deflated;

    bool want_deflate = ShouldTryDeflate(data.size(), impl_->options);
#if defined(NEROUED_3MF_HAS_ZLIB)
    if (want_deflate && !data.empty()) {
        if (TryDeflateRaw(data.data(), data.size(), impl_->options.deflate_level, deflated) &&
            deflated.size() < data.size()) {
            compression_method = kCompressionMethodDeflate;
            payload_data = deflated.data();
            payload_size = deflated.size();
        }
    }
#endif

    CentralDirEntry entry;
    entry.file_name = path;
    entry.compression_method = compression_method;
    entry.general_flags = 0;
    entry.crc32 = ComputeCrc32(data.data(), data.size());
    entry.uncompressed_size = data.size();
    entry.compressed_size = payload_size;
    entry.local_header_offset = static_cast<uint64_t>(impl_->SinkSize());
    entry.mod_time = impl_->dos_time;
    entry.mod_date = impl_->dos_date;

    impl_->WriteLocalHeader(path, compression_method, 0, entry.crc32,
                            static_cast<uint32_t>(entry.compressed_size),
                            static_cast<uint32_t>(entry.uncompressed_size));
    impl_->SinkAppendBytes(payload_data, payload_size);
    impl_->central_entries.push_back(std::move(entry));
}

void StreamingZipWriter::WriteWholeEntry(const std::string &path, const std::string &data) {
    auto span =
        std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(data.data()), data.size());
    WriteWholeEntry(path, span);
}

void StreamingZipWriter::BeginDeflateEntry(const std::string &path) {
    if (impl_->in_stream_entry) { throw InputError("Already in a streaming entry"); }
    impl_->in_stream_entry = true;
    impl_->stream_is_deflating = false;
    impl_->stream_filename = path;
    impl_->InitStreamCrc();
    impl_->stream_uncompressed = 0;

    bool want_deflate = (impl_->options.compression != WriteOptions::Compression::Store);

    uint16_t compression_method = kCompressionMethodStore;
#if defined(NEROUED_3MF_HAS_ZLIB)
    if (want_deflate) {
        impl_->zstream = z_stream{};
        int rc = deflateInit2(&impl_->zstream, impl_->options.deflate_level, Z_DEFLATED, -MAX_WBITS,
                              8, Z_DEFAULT_STRATEGY);
        if (rc == Z_OK) {
            impl_->zstream_active = true;
            impl_->stream_is_deflating = true;
            compression_method = kCompressionMethodDeflate;
        }
    }
#endif

    impl_->stream_local_header_offset = static_cast<uint64_t>(impl_->SinkSize());
    impl_->WriteLocalHeader(path, compression_method, kFlagDataDescriptor, 0, 0, 0);
    impl_->stream_data_start = impl_->SinkSize();
}

void StreamingZipWriter::WriteChunk(const void *data, std::size_t len) {
    if (!impl_->in_stream_entry) { throw InputError("Not in a streaming entry"); }
    if (len == 0) { return; }

    impl_->UpdateStreamCrc(data, len);
    impl_->stream_uncompressed += len;

#if defined(NEROUED_3MF_HAS_ZLIB)
    if (impl_->stream_is_deflating) {
        impl_->zstream.next_in = static_cast<Bytef *>(const_cast<void *>(data));
        impl_->zstream.avail_in = static_cast<uInt>(len);
        impl_->FlushDeflate(Z_NO_FLUSH);
        return;
    }
#endif
    impl_->SinkAppendBytes(data, len);
}

void StreamingZipWriter::EndEntry() {
    if (!impl_->in_stream_entry) { throw InputError("Not in a streaming entry"); }

    uint16_t compression_method = kCompressionMethodStore;
#if defined(NEROUED_3MF_HAS_ZLIB)
    if (impl_->stream_is_deflating) {
        impl_->zstream.next_in = nullptr;
        impl_->zstream.avail_in = 0;
        impl_->FlushDeflate(Z_FINISH);
        deflateEnd(&impl_->zstream);
        impl_->zstream_active = false;
        compression_method = kCompressionMethodDeflate;
    }
#endif

    uint32_t final_crc = impl_->FinalStreamCrc();
    uint64_t compressed_size = static_cast<uint64_t>(impl_->SinkSize() - impl_->stream_data_start);
    uint64_t uncompressed_size = impl_->stream_uncompressed;

    impl_->WriteDataDescriptor(final_crc, compressed_size, uncompressed_size);

    CentralDirEntry entry;
    entry.file_name = impl_->stream_filename;
    entry.compression_method = compression_method;
    entry.general_flags = kFlagDataDescriptor;
    entry.crc32 = final_crc;
    entry.compressed_size = compressed_size;
    entry.uncompressed_size = uncompressed_size;
    entry.local_header_offset = impl_->stream_local_header_offset;
    entry.mod_time = impl_->dos_time;
    entry.mod_date = impl_->dos_date;
    impl_->central_entries.push_back(std::move(entry));

    impl_->in_stream_entry = false;
    impl_->stream_is_deflating = false;
}

void StreamingZipWriter::Finalize() {
    if (impl_->in_stream_entry) { throw InputError("Cannot finalize while streaming an entry"); }
    impl_->WriteCentralDirectory();
}

} // namespace neroued_3mf::detail
