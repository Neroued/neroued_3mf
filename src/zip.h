// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

#include "neroued/3mf/writer.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace neroued_3mf::detail {

class StreamingZipWriter {
  public:
    StreamingZipWriter(std::vector<uint8_t> &output, const WriteOptions &options);
    StreamingZipWriter(const std::filesystem::path &file_path, const WriteOptions &options);
    StreamingZipWriter(std::ostream &output, const WriteOptions &options);
    ~StreamingZipWriter();

    StreamingZipWriter(const StreamingZipWriter &) = delete;
    StreamingZipWriter &operator=(const StreamingZipWriter &) = delete;

    void WriteWholeEntry(const std::string &path, const std::string &data);
    void WriteWholeEntry(const std::string &path, std::span<const uint8_t> data);
    void BeginDeflateEntry(const std::string &path);
    void WriteChunk(const void *data, std::size_t len);
    void EndEntry();
    void Finalize();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace neroued_3mf::detail
