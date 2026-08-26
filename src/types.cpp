// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#include "neroued/3mf/types.h"

#include "neroued/3mf/error.h"

#include "internal/mesh_analysis.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <limits>

namespace neroued_3mf {

namespace {

template <typename T> bool SpansOverlap(std::span<const T> source, const std::vector<T> &target) {
    if (source.empty() || target.empty()) { return false; }

    const T *source_begin = source.data();
    const T *source_end = source_begin + source.size();
    const T *target_begin = target.data();
    const T *target_end = target_begin + target.size();
    auto less = std::less<const T *>{};
    return less(source_begin, target_end) && less(target_begin, source_end);
}

} // namespace

// -- Color ------------------------------------------------------------------

std::string Color::ToHex() const {
    char buf[10];
    if (a == 255) {
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    } else {
        std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", r, g, b, a);
    }
    return buf;
}

Color Color::FromHex(const std::string &hex) {
    auto is_hex = [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; };
    auto hex_value = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') { return static_cast<uint8_t>(c - '0'); }
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c >= 'a' && c <= 'f') { return static_cast<uint8_t>(10 + (c - 'a')); }
        return 0;
    };
    if (hex.empty() || hex[0] != '#') { return {255, 255, 255, 255}; }
    for (std::size_t i = 1; i < hex.size(); ++i) {
        if (!is_hex(hex[i])) { return {255, 255, 255, 255}; }
    }

    auto parse2 = [&](std::size_t offset) -> uint8_t {
        if (offset + 1 >= hex.size()) { return 0; }
        return static_cast<uint8_t>((hex_value(hex[offset]) << 4U) | hex_value(hex[offset + 1]));
    };

    if (hex.size() == 7) { return {parse2(1), parse2(3), parse2(5), 255}; }
    if (hex.size() == 9) { return {parse2(1), parse2(3), parse2(5), parse2(7)}; }
    return {255, 255, 255, 255};
}

// -- Transform --------------------------------------------------------------

Transform Transform::Translation(float tx, float ty, float tz) {
    Transform t;
    t.m[9] = tx;
    t.m[10] = ty;
    t.m[11] = tz;
    return t;
}

bool Transform::IsIdentity(float eps) const {
    constexpr std::array<float, 12> identity = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (std::abs(m[i] - identity[i]) > eps) { return false; }
    }
    return true;
}

// -- Mesh -------------------------------------------------------------------

BBox Mesh::ComputeBoundingBox() const {
    return detail::ComputeBoundingBox(vertices);
}

void Mesh::Append(const MeshView &other) {
    if (other.vertices.empty()) {
        if (!other.triangles.empty() || !other.triangle_properties.empty()) {
            throw InputError("Source mesh has triangles or properties but no vertices");
        }
        return;
    }

    const bool self_alias = SpansOverlap(other.vertices, vertices) ||
                            SpansOverlap(other.triangles, triangles) ||
                            SpansOverlap(other.triangle_properties, triangle_properties);
    if (self_alias) {
        Mesh snapshot;
        snapshot.vertices.assign(other.vertices.begin(), other.vertices.end());
        snapshot.triangles.assign(other.triangles.begin(), other.triangles.end());
        snapshot.triangle_properties.assign(other.triangle_properties.begin(),
                                            other.triangle_properties.end());
        Append(snapshot.View());
        return;
    }

    if (!triangle_properties.empty() && triangle_properties.size() != triangles.size()) {
        throw InputError("Destination mesh triangle_properties size must match triangles size");
    }
    if (!other.triangle_properties.empty() &&
        other.triangle_properties.size() != other.triangles.size()) {
        throw InputError("Source mesh triangle_properties size must match triangles size");
    }
    if (!triangles.empty() && !other.triangles.empty() &&
        triangle_properties.empty() != other.triangle_properties.empty()) {
        throw InputError("Cannot append meshes with incompatible triangle property layouts");
    }

    constexpr std::size_t kMaxIndex = std::numeric_limits<uint32_t>::max();
    if (vertices.size() > kMaxIndex || other.vertices.size() > kMaxIndex - vertices.size()) {
        throw InputError("Appending mesh would exceed uint32 vertex index range");
    }

    auto v_offset = static_cast<uint32_t>(vertices.size());
    for (const auto &tri : other.triangles) {
        if (tri.v1 >= other.vertices.size() || tri.v2 >= other.vertices.size() ||
            tri.v3 >= other.vertices.size()) {
            throw InputError("Source mesh contains an out-of-range vertex index");
        }
        if (tri.v1 > std::numeric_limits<uint32_t>::max() - v_offset ||
            tri.v2 > std::numeric_limits<uint32_t>::max() - v_offset ||
            tri.v3 > std::numeric_limits<uint32_t>::max() - v_offset) {
            throw InputError("Appending mesh would overflow a triangle vertex index");
        }
    }

    vertices.reserve(vertices.size() + other.vertices.size());
    triangles.reserve(triangles.size() + other.triangles.size());
    if (!triangle_properties.empty() || !other.triangle_properties.empty()) {
        triangle_properties.reserve(triangle_properties.size() + other.triangle_properties.size());
    }

    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
    for (const auto &tri : other.triangles) {
        triangles.push_back({tri.v1 + v_offset, tri.v2 + v_offset, tri.v3 + v_offset});
    }

    if (!other.triangle_properties.empty()) {
        triangle_properties.insert(triangle_properties.end(), other.triangle_properties.begin(),
                                   other.triangle_properties.end());
    }
}

ValidationResult Mesh::Validate() const {
    auto analysis = AnalyzeMesh(View(), TriangleWriteMode::Keep);
    ValidationResult result;
    result.degenerate_count = analysis.degenerate_count;
    result.out_of_range_count = analysis.out_of_range_count;
    return result;
}

MeshAnalysis AnalyzeSignedTriangleBuffer(std::span<const Vec3f> vertices,
                                         std::span<const int32_t> triangle_indices,
                                         TriangleWriteMode mode) {
    if (triangle_indices.size() % 3 != 0) {
        throw InputError("Signed triangle index buffer size must be a multiple of 3");
    }

    MeshAnalysis result;
    result.bbox = detail::ComputeBoundingBox(vertices);
    result.source_triangle_count = triangle_indices.size() / 3;

    auto vcount = vertices.size();
    auto tri_count = result.source_triangle_count;

#if defined(NEROUED_3MF_HAS_OPENMP)
    if (tri_count > detail::kOmpValidateThreshold) {
        std::size_t degenerate = 0;
        std::size_t out_of_range = 0;
        std::size_t written = 0;
        auto n = static_cast<std::ptrdiff_t>(tri_count);

#pragma omp parallel for reduction(+ : degenerate, out_of_range, written) schedule(static)
        for (std::ptrdiff_t i = 0; i < n; ++i) {
            auto base = static_cast<std::size_t>(i) * 3;
            detail::SignedTriangle tri{
                triangle_indices[base],
                triangle_indices[base + 1],
                triangle_indices[base + 2],
            };
            if (detail::IsOutOfRange(tri, vcount)) {
                ++out_of_range;
                continue;
            }

            bool degenerate_tri = detail::IsDegenerateTriangle(tri, vertices);
            if (degenerate_tri) {
                ++degenerate;
                if (mode == TriangleWriteMode::Keep) { ++written; }
                continue;
            }
            ++written;
        }

        result.degenerate_count = degenerate;
        result.out_of_range_count = out_of_range;
        result.written_triangle_count = written;
        return result;
    }
#endif

    for (std::size_t i = 0; i < tri_count; ++i) {
        auto base = i * 3;
        detail::SignedTriangle tri{
            triangle_indices[base],
            triangle_indices[base + 1],
            triangle_indices[base + 2],
        };
        if (detail::IsOutOfRange(tri, vcount)) {
            ++result.out_of_range_count;
            continue;
        }

        bool degenerate_tri = detail::IsDegenerateTriangle(tri, vertices);
        if (degenerate_tri) {
            ++result.degenerate_count;
            if (mode == TriangleWriteMode::Keep) { ++result.written_triangle_count; }
            continue;
        }
        ++result.written_triangle_count;
    }
    return result;
}

MeshAnalysis AnalyzeMesh(MeshView mesh, TriangleWriteMode mode) {
    MeshAnalysis result;
    result.bbox = detail::ComputeBoundingBox(mesh.vertices);
    result.source_triangle_count = mesh.triangles.size();

    auto vcount = mesh.vertices.size();

#if defined(NEROUED_3MF_HAS_OPENMP)
    if (mesh.triangles.size() > detail::kOmpValidateThreshold) {
        std::size_t degenerate = 0;
        std::size_t out_of_range = 0;
        std::size_t written = 0;
        auto n = static_cast<std::ptrdiff_t>(mesh.triangles.size());

#pragma omp parallel for reduction(+ : degenerate, out_of_range, written) schedule(static)
        for (std::ptrdiff_t i = 0; i < n; ++i) {
            const auto &tri = mesh.triangles[static_cast<std::size_t>(i)];
            if (detail::IsOutOfRange(tri, vcount)) {
                ++out_of_range;
                continue;
            }

            bool degenerate_tri = detail::IsDegenerateTriangle(tri, mesh.vertices);
            if (degenerate_tri) {
                ++degenerate;
                if (mode == TriangleWriteMode::Keep) { ++written; }
                continue;
            }
            ++written;
        }

        result.degenerate_count = degenerate;
        result.out_of_range_count = out_of_range;
        result.written_triangle_count = written;
        return result;
    }
#endif

    for (const auto &tri : mesh.triangles) {
        if (detail::IsOutOfRange(tri, vcount)) {
            ++result.out_of_range_count;
            continue;
        }

        bool degenerate_tri = detail::IsDegenerateTriangle(tri, mesh.vertices);
        if (degenerate_tri) {
            ++result.degenerate_count;
            if (mode == TriangleWriteMode::Keep) { ++result.written_triangle_count; }
            continue;
        }
        ++result.written_triangle_count;
    }
    return result;
}

std::size_t Mesh::RemoveDegenerateTriangles() {
    auto vcount = vertices.size();
    std::size_t write = 0;
    bool has_props = !triangle_properties.empty();

    for (std::size_t i = 0; i < triangles.size(); ++i) {
        const auto &tri = triangles[i];
        if (detail::IsOutOfRange(tri, vcount)) { continue; }
        if (detail::IsDegenerateTriangle(tri, vertices)) { continue; }
        if (write != i) {
            triangles[write] = triangles[i];
            if (has_props && i < triangle_properties.size()) {
                triangle_properties[write] = triangle_properties[i];
            }
        }
        ++write;
    }
    std::size_t removed = triangles.size() - write;
    triangles.resize(write);
    if (has_props) { triangle_properties.resize(std::min(triangle_properties.size(), write)); }
    return removed;
}

} // namespace neroued_3mf
