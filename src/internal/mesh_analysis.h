// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

#include "neroued/3mf/types.h"

#include "omp_config.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace neroued_3mf::detail {

struct SignedTriangle {
    int32_t v1 = 0;
    int32_t v2 = 0;
    int32_t v3 = 0;
};

inline bool IsDegenerateByIndex(const IndexTriangle &tri) {
    return tri.v1 == tri.v2 || tri.v2 == tri.v3 || tri.v1 == tri.v3;
}

inline bool IsDegenerateByIndex(const SignedTriangle &tri) {
    return tri.v1 == tri.v2 || tri.v2 == tri.v3 || tri.v1 == tri.v3;
}

inline bool IsDegenerateByArea(const Vec3f &a, const Vec3f &b, const Vec3f &c) {
    float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    float acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
    float cx = aby * acz - abz * acy;
    float cy = abz * acx - abx * acz;
    float cz = abx * acy - aby * acx;
    return (cx * cx + cy * cy + cz * cz) <= 1e-12f;
}

inline BBox ComputeBoundingBox(std::span<const Vec3f> vertices) {
    if (vertices.empty()) { return {{0, 0, 0}, {0, 0, 0}}; }

    BBox box;
    box.min = box.max = vertices[0];

#if defined(NEROUED_3MF_HAS_OPENMP)
    if (vertices.size() > kOmpBBoxThreshold) {
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

inline bool IsOutOfRange(const IndexTriangle &tri, std::size_t vcount) {
    return tri.v1 >= vcount || tri.v2 >= vcount || tri.v3 >= vcount;
}

inline bool IsOutOfRange(const SignedTriangle &tri, std::size_t vcount) {
    return tri.v1 < 0 || tri.v2 < 0 || tri.v3 < 0 || static_cast<std::size_t>(tri.v1) >= vcount ||
           static_cast<std::size_t>(tri.v2) >= vcount || static_cast<std::size_t>(tri.v3) >= vcount;
}

inline bool IsDegenerateTriangle(const IndexTriangle &tri, std::span<const Vec3f> vertices) {
    return IsDegenerateByIndex(tri) ||
           IsDegenerateByArea(vertices[tri.v1], vertices[tri.v2], vertices[tri.v3]);
}

inline bool IsDegenerateTriangle(const SignedTriangle &tri, std::span<const Vec3f> vertices) {
    return IsDegenerateByIndex(tri) ||
           IsDegenerateByArea(vertices[static_cast<std::size_t>(tri.v1)],
                              vertices[static_cast<std::size_t>(tri.v2)],
                              vertices[static_cast<std::size_t>(tri.v3)]);
}

} // namespace neroued_3mf::detail
