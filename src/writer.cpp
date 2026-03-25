// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#include "neroued/3mf/writer.h"

#include "neroued/3mf/error.h"

#include "internal/validate.h"
#include "opc.h"
#include "zip.h"

#include <filesystem>
#include <fstream>
#include <ostream>
#include <random>
#include <system_error>

namespace neroued_3mf {

namespace {

std::string RandomHex(std::size_t byte_count) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::random_device rd;
    std::string out;
    out.reserve(byte_count * 2);
    for (std::size_t i = 0; i < byte_count; ++i) {
        auto value = static_cast<unsigned char>(rd());
        out.push_back(kHex[(value >> 4U) & 0x0F]);
        out.push_back(kHex[value & 0x0F]);
    }
    return out;
}

std::filesystem::path MakeAtomicTempPath(const std::filesystem::path &final_path) {
    auto dir = final_path.parent_path();
    auto base = final_path.filename().string();
    for (int attempt = 0; attempt < 16; ++attempt) {
        auto candidate = dir / (base + ".tmp-" + RandomHex(8));
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec) && !ec) { return candidate; }
    }
    throw IOError("Failed to allocate temporary output path for " + final_path.string());
}

class PendingAtomicFile {
  public:
    explicit PendingAtomicFile(std::filesystem::path path) : path_(std::move(path)) {}
    ~PendingAtomicFile() { Cleanup(); }

    PendingAtomicFile(const PendingAtomicFile &) = delete;
    PendingAtomicFile &operator=(const PendingAtomicFile &) = delete;

    const std::filesystem::path &path() const { return path_; }

    void CommitTo(const std::filesystem::path &final_path) {
        std::error_code ec;
#if defined(_WIN32)
        if (std::filesystem::exists(final_path, ec) && !ec) {
            std::filesystem::remove(final_path, ec);
            if (ec) {
                throw IOError("Failed to replace existing file " + final_path.string() + ": " +
                              ec.message());
            }
        }
#endif
        std::filesystem::rename(path_, final_path, ec);
        if (ec) {
            throw IOError("Failed to move temp file into place for " + final_path.string() + ": " +
                          ec.message());
        }
        path_.clear();
    }

  private:
    void Cleanup() noexcept {
        if (path_.empty()) { return; }
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        path_.clear();
    }

    std::filesystem::path path_;
};

std::size_t EstimateOutputSize(const Document &doc, const WriteOptions &opts) {
    std::size_t raw = 4096;
    for (const auto &obj : doc.objects) {
        raw += obj.mesh.vertices.size() * 40 + obj.mesh.triangles.size() * 35;
    }
    for (const auto &cp : doc.custom_parts) { raw += cp.data.size(); }
    if (doc.thumbnail.has_value()) { raw += doc.thumbnail->data.size(); }
    if (opts.compression != WriteOptions::Compression::Store) { raw = raw * 2 / 5; }
    return raw;
}

void WriteAllEntries(detail::StreamingZipWriter &zip, const Document &doc,
                     const WriteOptions &opts) {
    auto parts = detail::BuildOpcParts(doc);
    for (const auto &part : parts) {
        zip.WriteWholeEntry(part.path_in_zip, std::span<const uint8_t>(part.data));
    }

    auto zip_sink = [&](std::string_view chunk) { zip.WriteChunk(chunk.data(), chunk.size()); };

    if (doc.production.enabled) {
        if (doc.production.merge_objects) {
            Document merged_doc;
            merged_doc.unit = doc.unit;
            merged_doc.language = doc.language;
            merged_doc.objects = doc.objects;
            merged_doc.production = doc.production;
            merged_doc.custom_namespaces = doc.custom_namespaces;

            zip.BeginDeflateEntry("3D/Objects/object_1.model");
            detail::StreamMeshXml(merged_doc, detail::MeshXmlFormat::ObjectsModel, opts.compact_xml,
                                  zip_sink);
            zip.EndEntry();
        } else {
            for (std::size_t i = 0; i < doc.objects.size(); ++i) {
                std::string ext_path = "3D/Objects/object_" + std::to_string(i + 1) + ".model";

                Document single_doc;
                single_doc.unit = doc.unit;
                single_doc.language = doc.language;
                single_doc.objects = {doc.objects[i]};
                single_doc.production = doc.production;
                single_doc.custom_namespaces = doc.custom_namespaces;

                zip.BeginDeflateEntry(ext_path);
                detail::StreamMeshXml(single_doc, detail::MeshXmlFormat::ObjectsModel,
                                      opts.compact_xml, zip_sink);
                zip.EndEntry();
            }
        }
    } else {
        zip.BeginDeflateEntry("3D/3dmodel.model");
        detail::StreamMeshXml(doc, detail::MeshXmlFormat::FlatModel, opts.compact_xml, zip_sink);
        zip.EndEntry();
    }

    zip.Finalize();
}

void ValidateZipOutput(const std::vector<uint8_t> &zip_bytes) {
    constexpr std::size_t kMinZipSize = 22;
    if (zip_bytes.size() < kMinZipSize) {
        throw IOError("Generated 3MF buffer is too small (" + std::to_string(zip_bytes.size()) +
                      " bytes), likely corrupt");
    }
    if (zip_bytes[0] != 0x50 || zip_bytes[1] != 0x4B || zip_bytes[2] != 0x03 ||
        zip_bytes[3] != 0x04) {
        throw IOError("Generated 3MF buffer has invalid ZIP header");
    }
}

void ValidateDocumentPreWrite(const Document &doc) {
    detail::ValidateDocument(doc);
}

} // namespace

std::vector<uint8_t> WriteToBuffer(const Document &doc, const WriteOptions &opts) {
    ValidateDocumentPreWrite(doc);

    std::vector<uint8_t> zip_bytes;
    zip_bytes.reserve(EstimateOutputSize(doc, opts));
    detail::StreamingZipWriter zip(zip_bytes, opts);
    WriteAllEntries(zip, doc, opts);
    ValidateZipOutput(zip_bytes);
    return zip_bytes;
}

void WriteToFile(const std::string &path, const Document &doc, const WriteOptions &opts) {
    if (path.empty()) { throw InputError("3MF output path is empty"); }
    ValidateDocumentPreWrite(doc);

    auto final_path = std::filesystem::path(path);
    auto dir = final_path.parent_path();
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            throw IOError("Failed to create output directory " + dir.string() + ": " +
                          ec.message());
        }
    }

    PendingAtomicFile pending(MakeAtomicTempPath(final_path));
    {
        detail::StreamingZipWriter zip(pending.path().string(), opts);
        WriteAllEntries(zip, doc, opts);
    }
    pending.CommitTo(final_path);
}

void WriteToStream(std::ostream &out, const Document &doc, const WriteOptions &opts) {
    ValidateDocumentPreWrite(doc);

    detail::StreamingZipWriter zip(out, opts);
    WriteAllEntries(zip, doc, opts);
}

} // namespace neroued_3mf
