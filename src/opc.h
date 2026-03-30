// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

#include "neroued/3mf/document.h"

#include "internal/omp_config.h"
#include "internal/opc_types.h"
#include "internal/watermark.h"
#include "internal/xml_stream_buffer.h"
#include "internal/xml_util.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace neroued_3mf::detail {

inline constexpr std::string_view kCoreNs =
    "http://schemas.microsoft.com/3dmanufacturing/core/2015/02";

inline std::string_view UnitToStringView(Unit unit) {
    switch (unit) {
    case Unit::Micron:
        return "micron";
    case Unit::Millimeter:
        return "millimeter";
    case Unit::Centimeter:
        return "centimeter";
    case Unit::Inch:
        return "inch";
    case Unit::Foot:
        return "foot";
    case Unit::Meter:
        return "meter";
    }
    return "millimeter";
}

inline std::string_view ObjectTypeToStringView(ObjectType type) {
    switch (type) {
    case ObjectType::Model:
        return "model";
    case ObjectType::SolidSupport:
        return "solidsupport";
    case ObjectType::Support:
        return "support";
    case ObjectType::Surface:
        return "surface";
    case ObjectType::Other:
        return "other";
    }
    return "model";
}

std::vector<OpcPart> BuildOpcParts(const Document &doc);

enum class MeshXmlFormat { FlatModel, ObjectsModel };

/// Cyclic rotation map: kTriRotation[rot][pos] gives the index into {v1,v2,v3}.
/// rot=0 → identity, rot=1 → (v2,v3,v1), preserving CCW winding.
inline constexpr uint8_t kTriRotation[3][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1}};

/// Stream mesh data as XML fragments to a sink callback (zero-allocation hot path).
/// rotation_table: per-triangle rotation (0 or 1); empty = no rotation.
/// tri_offset: starting index into rotation_table for the first object in doc.
template <typename Sink>
void StreamMeshXml(const Document &doc, MeshXmlFormat format, bool compact, int vertex_precision,
                   std::span<const uint8_t> rotation_table, std::size_t tri_offset, Sink &&sink) {
    XmlStreamBuffer<std::remove_reference_t<Sink>> buf(sink, vertex_precision);

    const std::string_view i2 = compact ? "" : "  ";
    const std::string_view i4 = compact ? "" : "    ";
    const std::string_view i6 = compact ? "" : "      ";
    const std::string_view i8 = compact ? "" : "        ";
    const std::string_view i10 = compact ? "" : "          ";

    // -- XML header --
    buf.Append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<model unit=\"");
    buf.Append(UnitToStringView(doc.unit));
    buf.Append("\" xml:lang=\"");
    buf.Append(EscapeXml(doc.language));
    buf.Append("\" xmlns=\"");
    buf.Append(kCoreNs);
    buf.Append("\"");
    for (const auto &ns : doc.custom_namespaces) {
        if (ns.prefix.empty()) { continue; }
        buf.Append(" xmlns:");
        buf.Append(EscapeXml(ns.prefix));
        buf.Append("=\"");
        buf.Append(EscapeXml(ns.uri));
        buf.Append("\"");
    }
    buf.Append(">\n");

    // -- Metadata --
    if (format == MeshXmlFormat::ObjectsModel) {
        for (const auto &md : doc.production.external_model_metadata) {
            if (md.name.empty()) { continue; }
            buf.Append(i2);
            buf.Append("<metadata name=\"");
            buf.Append(EscapeXml(md.name));
            buf.Append("\"");
            if (!md.type.empty()) {
                buf.Append(" type=\"");
                buf.Append(EscapeXml(md.type));
                buf.Append("\"");
            }
            buf.Append(">");
            buf.Append(EscapeXml(md.value));
            buf.Append("</metadata>\n");
        }
    }

    if (format == MeshXmlFormat::FlatModel) {
        for (const auto &md : doc.metadata) {
            if (md.name.empty()) { continue; }
            buf.Append(i2);
            buf.Append("<metadata name=\"");
            buf.Append(EscapeXml(md.name));
            buf.Append("\"");
            if (!md.type.empty()) {
                buf.Append(" type=\"");
                buf.Append(EscapeXml(md.type));
                buf.Append("\"");
            }
            buf.Append(">");
            buf.Append(EscapeXml(md.value));
            buf.Append("</metadata>\n");
        }
        buf.Append(i2);
        buf.Append("<resources>\n");

        for (const auto &bmg : doc.base_material_groups) {
            buf.Append(i4);
            buf.Append("<basematerials id=\"");
            buf.AppendUint32(bmg.id);
            buf.Append("\">\n");
            for (const auto &mat : bmg.materials) {
                buf.Append(i6);
                buf.Append("<base name=\"");
                buf.Append(EscapeXml(mat.name));
                buf.Append("\" displaycolor=\"");
                buf.Append(EscapeXml(mat.display_color.ToHex()));
                buf.Append("\"/>\n");
            }
            buf.Append(i4);
            buf.Append("</basematerials>\n");
        }
    } else {
        buf.Append(i2);
        buf.Append("<resources>\n");
    }

    // -- Objects (hot path: zero heap allocations per vertex/triangle) --
    std::size_t obj_tri_base = tri_offset;
    for (const auto &obj : doc.objects) {
        buf.Append(i4);
        buf.Append("<object id=\"");
        buf.AppendUint32(obj.id);
        buf.Append("\"");
        if (!obj.name.empty()) {
            buf.Append(" name=\"");
            buf.Append(EscapeXml(obj.name));
            buf.Append("\"");
        }
        if (!obj.partnumber.empty()) {
            buf.Append(" partnumber=\"");
            buf.Append(EscapeXml(obj.partnumber));
            buf.Append("\"");
        }
        buf.Append(" type=\"");
        buf.Append(ObjectTypeToStringView(obj.type));
        buf.Append("\"");
        if (obj.pid.has_value()) {
            buf.Append(" pid=\"");
            buf.AppendUint32(*obj.pid);
            buf.Append("\"");
            if (obj.pindex.has_value()) {
                buf.Append(" pindex=\"");
                buf.AppendUint32(*obj.pindex);
                buf.Append("\"");
            }
        }
        if (!obj.uuid.empty()) {
            buf.Append(" p:UUID=\"");
            buf.Append(EscapeXml(obj.uuid));
            buf.Append("\"");
        }
        buf.Append(">\n");

        // -- Per-object metadata --
        if (!obj.metadata.empty()) {
            buf.Append(i6);
            buf.Append("<metadatagroup>\n");
            for (const auto &md : obj.metadata) {
                if (md.name.empty()) { continue; }
                buf.Append(i8);
                buf.Append("<metadata name=\"");
                buf.Append(EscapeXml(md.name));
                buf.Append("\"");
                if (!md.type.empty()) {
                    buf.Append(" type=\"");
                    buf.Append(EscapeXml(md.type));
                    buf.Append("\"");
                }
                buf.Append(">");
                buf.Append(EscapeXml(md.value));
                buf.Append("</metadata>\n");
            }
            buf.Append(i6);
            buf.Append("</metadatagroup>\n");
        }

        // -- Mesh or Components (mutually exclusive) --
        if (!obj.mesh.vertices.empty()) {
            const auto &mesh = obj.mesh;
            buf.Append(i6);
            buf.Append("<mesh>\n");
            buf.Append(i8);
            buf.Append("<vertices>\n");

#if defined(NEROUED_3MF_HAS_OPENMP)
            if (mesh.vertices.size() > kOmpMeshXmlThreshold) {
                int max_threads = omp_get_max_threads();
                std::vector<std::string> vbufs(max_threads);
                auto nv = static_cast<std::ptrdiff_t>(mesh.vertices.size());
                std::size_t est = static_cast<std::size_t>(nv / max_threads + 1) * 80;
                for (auto &vb : vbufs) { vb.reserve(est); }

#pragma omp parallel for schedule(static)
                for (std::ptrdiff_t vi = 0; vi < nv; ++vi) {
                    auto &local = vbufs[omp_get_thread_num()];
                    const auto &v = mesh.vertices[static_cast<std::size_t>(vi)];
                    local += i10;
                    local += "<vertex x=\"";
                    AppendFloat(local, v.x, vertex_precision);
                    local += "\" y=\"";
                    AppendFloat(local, v.y, vertex_precision);
                    local += "\" z=\"";
                    AppendFloat(local, v.z, vertex_precision);
                    local += "\"/>\n";
                }
                for (const auto &vb : vbufs) {
                    if (!vb.empty()) { buf.Append(vb); }
                }
            } else
#endif
            {
                for (std::size_t vi = 0; vi < mesh.vertices.size(); ++vi) {
                    const auto &v = mesh.vertices[vi];
                    buf.Append(i10);
                    buf.Append("<vertex x=\"");
                    buf.AppendFloat(v.x);
                    buf.Append("\" y=\"");
                    buf.AppendFloat(v.y);
                    buf.Append("\" z=\"");
                    buf.AppendFloat(v.z);
                    buf.Append("\"/>\n");
                }
            }

            buf.Append(i8);
            buf.Append("</vertices>\n");
            buf.Append(i8);
            buf.Append("<triangles>\n");

            bool has_props = !mesh.triangle_properties.empty();
#if defined(NEROUED_3MF_HAS_OPENMP)
            if (mesh.triangles.size() > kOmpMeshXmlThreshold) {
                int max_threads = omp_get_max_threads();
                std::vector<std::string> tbufs(max_threads);
                auto nt = static_cast<std::ptrdiff_t>(mesh.triangles.size());
                std::size_t est = static_cast<std::size_t>(nt / max_threads + 1) * 100;
                for (auto &tb : tbufs) { tb.reserve(est); }

                const bool has_rot = !rotation_table.empty();
#pragma omp parallel for schedule(static)
                for (std::ptrdiff_t ti = 0; ti < nt; ++ti) {
                    auto idx = static_cast<std::size_t>(ti);
                    auto &local = tbufs[omp_get_thread_num()];
                    const auto &tri = mesh.triangles[idx];

                    uint32_t vi[3] = {tri.v1, tri.v2, tri.v3};
                    uint8_t rot = 0;
                    if (has_rot) {
                        uint8_t canon = CanonicalRotation(vi[0], vi[1], vi[2]);
                        rot =
                            static_cast<uint8_t>((canon + rotation_table[obj_tri_base + idx]) % 3);
                    }
                    const auto *rm = kTriRotation[rot];

                    local += i10;
                    local += "<triangle v1=\"";
                    AppendUint32(local, vi[rm[0]]);
                    local += "\" v2=\"";
                    AppendUint32(local, vi[rm[1]]);
                    local += "\" v3=\"";
                    AppendUint32(local, vi[rm[2]]);
                    local += "\"";
                    if (has_props && idx < mesh.triangle_properties.size()) {
                        const auto &tp = mesh.triangle_properties[idx];
                        uint32_t pi[3] = {tp.p1, tp.p2, tp.p3};
                        local += " pid=\"";
                        AppendUint32(local, tp.pid);
                        local += "\" p1=\"";
                        AppendUint32(local, pi[rm[0]]);
                        local += "\" p2=\"";
                        AppendUint32(local, pi[rm[1]]);
                        local += "\" p3=\"";
                        AppendUint32(local, pi[rm[2]]);
                        local += "\"";
                    }
                    local += "/>\n";
                }
                for (const auto &tb : tbufs) {
                    if (!tb.empty()) { buf.Append(tb); }
                }
            } else
#endif
            {
                const bool has_rot = !rotation_table.empty();
                for (std::size_t ti = 0; ti < mesh.triangles.size(); ++ti) {
                    const auto &tri = mesh.triangles[ti];

                    uint32_t vi[3] = {tri.v1, tri.v2, tri.v3};
                    uint8_t rot = 0;
                    if (has_rot) {
                        uint8_t canon = CanonicalRotation(vi[0], vi[1], vi[2]);
                        rot = static_cast<uint8_t>((canon + rotation_table[obj_tri_base + ti]) % 3);
                    }
                    const auto *rm = kTriRotation[rot];

                    buf.Append(i10);
                    buf.Append("<triangle v1=\"");
                    buf.AppendUint32(vi[rm[0]]);
                    buf.Append("\" v2=\"");
                    buf.AppendUint32(vi[rm[1]]);
                    buf.Append("\" v3=\"");
                    buf.AppendUint32(vi[rm[2]]);
                    buf.Append("\"");
                    if (has_props && ti < mesh.triangle_properties.size()) {
                        const auto &tp = mesh.triangle_properties[ti];
                        uint32_t pi[3] = {tp.p1, tp.p2, tp.p3};
                        buf.Append(" pid=\"");
                        buf.AppendUint32(tp.pid);
                        buf.Append("\" p1=\"");
                        buf.AppendUint32(pi[rm[0]]);
                        buf.Append("\" p2=\"");
                        buf.AppendUint32(pi[rm[1]]);
                        buf.Append("\" p3=\"");
                        buf.AppendUint32(pi[rm[2]]);
                        buf.Append("\"");
                    }
                    buf.Append("/>\n");
                }
            }

            buf.Append(i8);
            buf.Append("</triangles>\n");
            buf.Append(i6);
            buf.Append("</mesh>\n");
            obj_tri_base += mesh.triangles.size();
        } else if (!obj.components.empty()) {
            buf.Append(i6);
            buf.Append("<components>\n");
            for (const auto &comp : obj.components) {
                buf.Append(i8);
                buf.Append("<component objectid=\"");
                buf.AppendUint32(comp.object_id);
                buf.Append("\"");
                if (!comp.transform.IsIdentity()) {
                    buf.Append(" transform=\"");
                    buf.Append(SerializeTransform(comp.transform));
                    buf.Append("\"");
                }
                buf.Append("/>\n");
            }
            buf.Append(i6);
            buf.Append("</components>\n");
        }

        buf.Append(i4);
        buf.Append("</object>\n");
    }

    // -- Footer --
    if (format == MeshXmlFormat::FlatModel) {
        buf.Append(i2);
        buf.Append("</resources>\n");
        buf.Append(i2);
        buf.Append("<build>\n");
        for (const auto &item : doc.build_items) {
            buf.Append(i4);
            buf.Append("<item objectid=\"");
            buf.AppendUint32(item.object_id);
            buf.Append("\"");
            if (!item.partnumber.empty()) {
                buf.Append(" partnumber=\"");
                buf.Append(EscapeXml(item.partnumber));
                buf.Append("\"");
            }
            if (!item.transform.IsIdentity()) {
                buf.Append(" transform=\"");
                buf.Append(SerializeTransform(item.transform));
                buf.Append("\"");
            }
            if (!item.uuid.empty()) {
                buf.Append(" p:UUID=\"");
                buf.Append(EscapeXml(item.uuid));
                buf.Append("\"");
            }
            buf.Append("/>\n");
        }
        buf.Append(i2);
        buf.Append("</build>\n");
    } else {
        buf.Append(i2);
        buf.Append("</resources>\n");
        buf.Append(i2);
        buf.Append("<build/>\n");
    }

    buf.Append("</model>\n");
    buf.Flush();
}

} // namespace neroued_3mf::detail
