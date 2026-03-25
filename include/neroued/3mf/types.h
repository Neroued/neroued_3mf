// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file types.h
/// \brief Core geometric types for 3MF mesh representation.
///
/// Provides both owning (Mesh) and non-owning (MeshView) mesh types.
/// MeshView enables zero-copy export from external vertex/index buffers.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace neroued_3mf {

struct Vec3f {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    bool operator==(const Vec3f &) const = default;
    bool IsFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};

struct IndexTriangle {
    uint32_t v1 = 0, v2 = 0, v3 = 0;

    bool operator==(const IndexTriangle &) const = default;
};

/// Per-triangle property override (3MF Core Spec 4.1.4).
/// References a property group (pid) and per-vertex property indices (p1, p2, p3).
struct TriangleProperty {
    uint32_t pid = 0;
    uint32_t p1 = 0, p2 = 0, p3 = 0;

    bool operator==(const TriangleProperty &) const = default;
};

/// Non-owning view over external vertex/index buffers.
/// Caller must keep the referenced data alive during Write().
struct MeshView {
    std::span<const Vec3f> vertices;
    std::span<const IndexTriangle> triangles;
    /// Optional per-triangle properties. Empty = use object-level pid/pindex.
    /// When non-empty, size must equal triangles.size().
    std::span<const TriangleProperty> triangle_properties;
};

/// RGBA color.
struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    bool operator==(const Color &) const = default;
    std::string ToHex() const;
    static Color FromHex(const std::string &hex);
};

/// 3MF affine transform: 3x3 rotation (row-major) + translation (tx, ty, tz).
struct Transform {
    std::array<float, 12> m = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};

    bool operator==(const Transform &) const = default;
    static Transform Identity() { return {}; }
    static Transform Translation(float tx, float ty, float tz);
    bool IsIdentity(float eps = 1e-6f) const;
};

/// Bounding box result.
struct BBox {
    Vec3f min, max;
};

/// Mesh validation result.
struct ValidationResult {
    std::size_t degenerate_count = 0;
    std::size_t out_of_range_count = 0;
    bool Valid() const { return out_of_range_count == 0; }
};

/// Owning triangle mesh with utility methods.
struct Mesh {
    std::vector<Vec3f> vertices;
    std::vector<IndexTriangle> triangles;
    std::vector<TriangleProperty> triangle_properties;

    operator MeshView() const { return View(); }

    MeshView View() const { return MeshView{vertices, triangles, triangle_properties}; }

    bool Empty() const { return vertices.empty() || triangles.empty(); }
    std::size_t VertexCount() const { return vertices.size(); }
    std::size_t TriangleCount() const { return triangles.size(); }
    bool HasTriangleProperties() const { return !triangle_properties.empty(); }

    BBox ComputeBoundingBox() const;
    void Append(const MeshView &other);
    ValidationResult Validate() const;
    /// Remove degenerate triangles in-place. Returns number removed.
    std::size_t RemoveDegenerateTriangles();
};

} // namespace neroued_3mf
