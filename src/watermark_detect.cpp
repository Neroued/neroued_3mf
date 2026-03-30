// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#include "neroued/3mf/watermark.h"

#include "internal/watermark.h"
#include "internal/zip_read.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace neroued_3mf {

namespace {

constexpr uint8_t kL2Signature[] = {
    0x33, 0x4E, 0x08, 0x00, 0x6E, 0x65, 0x72, 0x6F, 0x75, 0x65, 0x64, 0x00,
};

bool IsModelEntry(const std::string &path) {
    return path == "3D/3dmodel.model" ||
           (path.starts_with("3D/Objects/") && path.ends_with(".model"));
}

int ExtractModelNumber(const std::string &path) {
    static constexpr std::string_view kPrefix = "3D/Objects/object_";
    static constexpr std::string_view kSuffix = ".model";
    if (!path.starts_with(kPrefix) || !path.ends_with(kSuffix)) return 0;
    int val = 0;
    for (std::size_t i = kPrefix.size(); i < path.size() - kSuffix.size(); ++i) {
        char c = path[i];
        if (c < '0' || c > '9') return 0;
        val = val * 10 + (c - '0');
    }
    return val;
}

} // namespace

bool HasL2Signature(std::span<const uint8_t> data) {
    if (data.size() < sizeof(kL2Signature)) return false;
    return std::search(data.begin(), data.end(), std::begin(kL2Signature),
                       std::end(kL2Signature)) != data.end();
}

WatermarkResult DetectWatermark(std::span<const uint8_t> data, const std::vector<uint8_t> &key) {
    WatermarkResult result;
    result.has_l2_signature = HasL2Signature(data);

    auto entries = detail::FindZipEntries(data);
    if (entries.empty()) return result;

    struct ModelRef {
        int order;
        const detail::ZipEntry *entry;
    };
    std::vector<ModelRef> models;
    for (const auto &e : entries) {
        if (!IsModelEntry(e.path)) continue;
        int order = (e.path == "3D/3dmodel.model") ? 0 : ExtractModelNumber(e.path);
        models.push_back({order, &e});
    }
    std::sort(models.begin(), models.end(),
              [](const ModelRef &a, const ModelRef &b) { return a.order < b.order; });

    std::vector<uint8_t> rotations;
    for (const auto &m : models) {
        auto xml_data = detail::ExtractEntry(data, *m.entry);
        if (!xml_data) continue;

        std::string_view xml(reinterpret_cast<const char *>(xml_data->data()), xml_data->size());
        auto triangles = detail::ScanTrianglesFromXml(xml);

        for (const auto &tri : triangles) {
            uint8_t canon = detail::CanonicalRotation(tri[0], tri[1], tri[2]);
            rotations.push_back(static_cast<uint8_t>((3 - canon) % 3));
        }
    }

    if (rotations.empty()) return result;

    bool truncated = false;
    auto payload = detail::DecodePayload(key, rotations, &truncated);
    if (!payload.empty()) {
        result.has_l1_payload = true;
        result.payload_truncated = truncated;
        result.payload = std::move(payload);
    }

    return result;
}

} // namespace neroued_3mf
