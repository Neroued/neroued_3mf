// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#include "neroued/3mf/types.h"

#include "neroued/3mf/error.h"

#include "internal/omp_config.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>

namespace neroued_3mf {

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
    if (hex.empty() || hex[0] != '#') { return {255, 255, 255, 255}; }
    for (std::size_t i = 1; i < hex.size(); ++i) {
        if (!is_hex(hex[i])) { return {255, 255, 255, 255}; }
    }

    auto parse2 = [&](std::size_t offset) -> uint8_t {
        if (offset + 1 >= hex.size()) { return 0; }
        unsigned val = 0;
        auto [ptr, ec] = std::from_chars(hex.data() + offset, hex.data() + offset + 2, val, 16);
        if (ec != std::errc()) { return 0; }
        return static_cast<uint8_t>(val);
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

namespace {

bool IsDegenerateByIndex(const IndexTriangle &tri) {
    return tri.v1 == tri.v2 || tri.v2 == tri.v3 || tri.v1 == tri.v3;
}

bool IsDegenerateByArea(const Vec3f &a, const Vec3f &b, const Vec3f &c) {
    float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    float acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
    float cx = aby * acz - abz * acy;
    float cy = abz * acx - abx * acz;
    float cz = abx * acy - aby * acx;
    return (cx * cx + cy * cy + cz * cz) <= 1e-12f;
}

} // namespace

BBox Mesh::ComputeBoundingBox() const {
    if (vertices.empty()) { return {{0, 0, 0}, {0, 0, 0}}; }

    BBox box;
    box.min = box.max = vertices[0];

#if defined(NEROUED_3MF_HAS_OPENMP)
    if (vertices.size() > detail::kOmpBBoxThreshold) {
        float gmin_x = box.min.x, gmin_y = box.min.y, gmin_z = box.min.z;
        float gmax_x = box.max.x, gmax_y = box.max.y, gmax_z = box.max.z;
        auto n = static_cast<std::ptrdiff_t>(vertices.size());

#pragma omp parallel
        {
            float lmin_x = gmin_x, lmin_y = gmin_y, lmin_z = gmin_z;
            float lmax_x = gmax_x, lmax_y = gmax_y, lmax_z = gmax_z;

#pragma omp for nowait schedule(static)
            for (std::ptrdiff_t i = 1; i < n; ++i) {
                const auto &v = vertices[static_cast<std::size_t>(i)];
                if (v.x < lmin_x) lmin_x = v.x;
                if (v.y < lmin_y) lmin_y = v.y;
                if (v.z < lmin_z) lmin_z = v.z;
                if (v.x > lmax_x) lmax_x = v.x;
                if (v.y > lmax_y) lmax_y = v.y;
                if (v.z > lmax_z) lmax_z = v.z;
            }

#pragma omp critical
            {
                if (lmin_x < gmin_x) gmin_x = lmin_x;
                if (lmin_y < gmin_y) gmin_y = lmin_y;
                if (lmin_z < gmin_z) gmin_z = lmin_z;
                if (lmax_x > gmax_x) gmax_x = lmax_x;
                if (lmax_y > gmax_y) gmax_y = lmax_y;
                if (lmax_z > gmax_z) gmax_z = lmax_z;
            }
        }

        box.min = {gmin_x, gmin_y, gmin_z};
        box.max = {gmax_x, gmax_y, gmax_z};
        return box;
    }
#endif

    for (std::size_t i = 1; i < vertices.size(); ++i) {
        const auto &v = vertices[i];
        box.min.x = std::min(box.min.x, v.x);
        box.min.y = std::min(box.min.y, v.y);
        box.min.z = std::min(box.min.z, v.z);
        box.max.x = std::max(box.max.x, v.x);
        box.max.y = std::max(box.max.y, v.y);
        box.max.z = std::max(box.max.z, v.z);
    }
    return box;
}

void Mesh::Append(const MeshView &other) {
    if (other.vertices.empty()) { return; }

    auto v_offset = static_cast<uint32_t>(vertices.size());
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());

    std::size_t tri_start = triangles.size();
    triangles.reserve(triangles.size() + other.triangles.size());
    for (const auto &tri : other.triangles) {
        triangles.push_back({tri.v1 + v_offset, tri.v2 + v_offset, tri.v3 + v_offset});
    }

    if (!other.triangle_properties.empty()) {
        if (triangle_properties.size() < tri_start) { triangle_properties.resize(tri_start, {}); }
        triangle_properties.insert(triangle_properties.end(), other.triangle_properties.begin(),
                                   other.triangle_properties.end());
    }
}

ValidationResult Mesh::Validate() const {
    ValidationResult result;
    auto vcount = vertices.size();

#if defined(NEROUED_3MF_HAS_OPENMP)
    if (triangles.size() > detail::kOmpValidateThreshold) {
        std::size_t degenerate = 0;
        std::size_t out_of_range = 0;
        auto n = static_cast<std::ptrdiff_t>(triangles.size());

#pragma omp parallel for reduction(+ : degenerate, out_of_range) schedule(static)
        for (std::ptrdiff_t i = 0; i < n; ++i) {
            const auto &tri = triangles[static_cast<std::size_t>(i)];
            if (tri.v1 >= vcount || tri.v2 >= vcount || tri.v3 >= vcount) {
                ++out_of_range;
            } else if (IsDegenerateByIndex(tri)) {
                ++degenerate;
            } else if (IsDegenerateByArea(vertices[tri.v1], vertices[tri.v2], vertices[tri.v3])) {
                ++degenerate;
            }
        }

        result.degenerate_count = degenerate;
        result.out_of_range_count = out_of_range;
        return result;
    }
#endif

    for (const auto &tri : triangles) {
        if (tri.v1 >= vcount || tri.v2 >= vcount || tri.v3 >= vcount) {
            ++result.out_of_range_count;
            continue;
        }
        if (IsDegenerateByIndex(tri)) {
            ++result.degenerate_count;
            continue;
        }
        if (IsDegenerateByArea(vertices[tri.v1], vertices[tri.v2], vertices[tri.v3])) {
            ++result.degenerate_count;
        }
    }
    return result;
}

std::size_t Mesh::RemoveDegenerateTriangles() {
    auto vcount = vertices.size();
    std::size_t write = 0;
    bool has_props = !triangle_properties.empty();

    for (std::size_t i = 0; i < triangles.size(); ++i) {
        const auto &tri = triangles[i];
        if (tri.v1 >= vcount || tri.v2 >= vcount || tri.v3 >= vcount) { continue; }
        if (IsDegenerateByIndex(tri)) { continue; }
        if (IsDegenerateByArea(vertices[tri.v1], vertices[tri.v2], vertices[tri.v3])) { continue; }
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
