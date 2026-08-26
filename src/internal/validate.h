// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

#include "neroued/3mf/document.h"
#include "neroued/3mf/error.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace neroued_3mf::detail {

enum class VisitState : uint8_t { Unvisited, Visiting, Visited };

inline constexpr uint64_t kCoreCollectionLimit = uint64_t{1} << 31U;

inline void ValidateCoreCount(std::size_t count, std::string_view label) {
    if (static_cast<uint64_t>(count) >= kCoreCollectionLimit) {
        throw InputError(std::string(label) + " must contain fewer than 2^31 entries");
    }
}

inline void ValidateResourceId(uint32_t id, std::string_view label) {
    if (id == 0 || static_cast<uint64_t>(id) >= kCoreCollectionLimit) {
        throw InputError(std::string(label) + " must be in the range [1, 2^31)");
    }
}

inline void ValidateTransform(const Transform &transform, std::string_view label) {
    for (float value : transform.m) {
        if (!std::isfinite(value)) {
            throw InputError(std::string(label) + " contains a non-finite value");
        }
    }
}

inline std::unordered_set<std::string> ValidateNamespaces(const Document &doc) {
    std::unordered_set<std::string> prefixes;
    prefixes.reserve(doc.custom_namespaces.size());
    for (const auto &ns : doc.custom_namespaces) {
        if (ns.prefix.empty()) { throw InputError("XML namespace prefix is empty"); }
        if (ns.uri.empty()) {
            throw InputError("XML namespace \"" + ns.prefix + "\" has an empty URI");
        }
        if (ns.prefix == "xml" || ns.prefix == "xmlns") {
            throw InputError("XML namespace prefix \"" + ns.prefix + "\" is reserved");
        }
        if (!prefixes.insert(ns.prefix).second) {
            throw InputError("Duplicate XML namespace prefix \"" + ns.prefix + "\"");
        }
    }
    return prefixes;
}

inline void ValidateMetadata(const std::vector<Metadata> &metadata,
                             const std::unordered_set<std::string> &namespace_prefixes,
                             std::string_view label) {
    ValidateCoreCount(metadata.size(), label);

    std::unordered_set<std::string> names;
    names.reserve(metadata.size());
    for (const auto &entry : metadata) {
        if (entry.name.empty()) {
            throw InputError(std::string(label) + " contains an empty name");
        }
        if (!names.insert(entry.name).second) {
            throw InputError(std::string(label) + " contains duplicate name \"" + entry.name +
                             "\"");
        }

        auto colon = entry.name.find(':');
        if (colon == std::string::npos) { continue; }
        if (colon == 0 || colon + 1 == entry.name.size() ||
            entry.name.find(':', colon + 1) != std::string::npos) {
            throw InputError(std::string(label) + " contains invalid qualified name \"" +
                             entry.name + "\"");
        }
        std::string prefix = entry.name.substr(0, colon);
        if (!namespace_prefixes.contains(prefix)) {
            throw InputError(std::string(label) + " uses undeclared namespace prefix \"" + prefix +
                             "\"");
        }
    }
}

inline void DetectComponentCycles(uint32_t id,
                                  const std::unordered_map<uint32_t, std::vector<uint32_t>> &adj,
                                  std::unordered_map<uint32_t, VisitState> &state,
                                  const std::unordered_map<uint32_t, std::string> &id_to_name) {
    state[id] = VisitState::Visiting;
    auto it = adj.find(id);
    if (it != adj.end()) {
        for (uint32_t child : it->second) {
            if (state[child] == VisitState::Visiting) {
                throw InputError("Circular component reference detected involving object \"" +
                                 id_to_name.at(child) + "\" (id " + std::to_string(child) + ")");
            }
            if (state[child] == VisitState::Unvisited) {
                DetectComponentCycles(child, adj, state, id_to_name);
            }
        }
    }
    state[id] = VisitState::Visited;
}

inline void ValidateDocument(const Document &doc, bool reject_repeated_vertex_indices = false) {
    if (doc.objects.empty()) { throw InputError("Document has no objects"); }
    if (doc.build_items.empty()) { throw InputError("Document has no build items"); }

    ValidateCoreCount(doc.objects.size(), "Document objects");
    ValidateCoreCount(doc.build_items.size(), "Document build items");
    ValidateCoreCount(doc.base_material_groups.size(), "Document base material groups");

    auto namespace_prefixes = ValidateNamespaces(doc);
    ValidateMetadata(doc.metadata, namespace_prefixes, "Document metadata");

    std::unordered_set<uint32_t> object_ids;
    object_ids.reserve(doc.objects.size());

    std::unordered_set<uint32_t> resource_ids;
    resource_ids.reserve(doc.objects.size() + doc.base_material_groups.size());

    std::unordered_map<uint32_t, std::size_t> material_group_sizes;
    material_group_sizes.reserve(doc.base_material_groups.size());
    for (const auto &bmg : doc.base_material_groups) {
        ValidateResourceId(bmg.id, "Base material group id");
        if (!resource_ids.insert(bmg.id).second) {
            throw InputError("Duplicate resource id " + std::to_string(bmg.id));
        }
        if (bmg.materials.empty()) {
            throw InputError("Base material group " + std::to_string(bmg.id) + " is empty");
        }
        ValidateCoreCount(bmg.materials.size(), "Base material group");
        material_group_sizes.emplace(bmg.id, bmg.materials.size());
    }

    for (const auto &obj : doc.objects) {
        ValidateResourceId(obj.id, "Object id");
        if (!resource_ids.insert(obj.id).second) {
            throw InputError("Duplicate resource id " + std::to_string(obj.id));
        }
        object_ids.insert(obj.id);
    }

    for (const auto &item : doc.build_items) {
        if (!object_ids.contains(item.object_id)) {
            throw InputError("BuildItem references non-existent object id " +
                             std::to_string(item.object_id));
        }
        ValidateTransform(item.transform,
                          "BuildItem for object " + std::to_string(item.object_id) + " transform");
    }

    for (const auto &obj : doc.objects) {
        const std::string object_label =
            "Object \"" + obj.name + "\" (id " + std::to_string(obj.id) + ")";
        bool has_mesh = !obj.mesh.vertices.empty() || !obj.mesh.triangles.empty();
        bool has_components = !obj.components.empty();

        if (has_mesh && has_components) {
            throw InputError(object_label +
                             " has both mesh and components; they are mutually exclusive");
        }
        if (!has_mesh && !has_components) {
            throw InputError(object_label + " has neither mesh nor components");
        }

        ValidateMetadata(obj.metadata, namespace_prefixes, object_label + " metadata");

        if (obj.pindex.has_value() && !obj.pid.has_value()) {
            throw InputError(object_label + " specifies pindex without pid");
        }
        if (has_components && (obj.pid.has_value() || obj.pindex.has_value())) {
            throw InputError(object_label + " contains components and must not specify pid/pindex");
        }

        if (has_mesh) {
            if (obj.mesh.vertices.empty() || obj.mesh.triangles.empty()) {
                throw InputError(object_label +
                                 " has an incomplete mesh (missing vertices or triangles)");
            }
            if (obj.mesh.vertices.size() < 3) {
                throw InputError(object_label + " mesh must contain at least 3 vertices");
            }

            ValidateCoreCount(obj.mesh.vertices.size(), object_label + " vertices");
            ValidateCoreCount(obj.mesh.triangles.size(), object_label + " triangles");

            for (std::size_t vi = 0; vi < obj.mesh.vertices.size(); ++vi) {
                if (!obj.mesh.vertices[vi].IsFinite()) {
                    throw InputError(object_label + " vertex " + std::to_string(vi) +
                                     " contains a non-finite coordinate");
                }
            }

            auto vcount = obj.mesh.vertices.size();
            for (std::size_t ti = 0; ti < obj.mesh.triangles.size(); ++ti) {
                const auto &tri = obj.mesh.triangles[ti];
                if (tri.v1 >= vcount || tri.v2 >= vcount || tri.v3 >= vcount) {
                    throw InputError(object_label + " triangle " + std::to_string(ti) +
                                     " has an out-of-range vertex index");
                }
                if (reject_repeated_vertex_indices &&
                    (tri.v1 == tri.v2 || tri.v1 == tri.v3 || tri.v2 == tri.v3)) {
                    throw InputError(object_label + " triangle " + std::to_string(ti) +
                                     " repeats a vertex index; use FilterDegenerate to skip it");
                }
            }

            if (!obj.mesh.triangle_properties.empty() &&
                obj.mesh.triangle_properties.size() != obj.mesh.triangles.size()) {
                throw InputError(object_label + " triangle_properties size (" +
                                 std::to_string(obj.mesh.triangle_properties.size()) +
                                 ") != triangles size (" +
                                 std::to_string(obj.mesh.triangles.size()) + ")");
            }

            if (!obj.mesh.triangle_properties.empty() &&
                (!obj.pid.has_value() || !obj.pindex.has_value())) {
                throw InputError(object_label +
                                 " has triangle properties but no default pid/pindex");
            }

            for (std::size_t ti = 0; ti < obj.mesh.triangle_properties.size(); ++ti) {
                const auto &tp = obj.mesh.triangle_properties[ti];
                auto group = material_group_sizes.find(tp.pid);
                if (group == material_group_sizes.end()) {
                    throw InputError(object_label + " triangle_property " + std::to_string(ti) +
                                     " references non-existent material group " +
                                     std::to_string(tp.pid));
                }
                if (tp.p1 >= group->second || tp.p2 >= group->second || tp.p3 >= group->second) {
                    throw InputError(object_label + " triangle_property " + std::to_string(ti) +
                                     " has an out-of-range material index");
                }
                if (tp.p1 != tp.p2 || tp.p1 != tp.p3) {
                    throw InputError(object_label + " triangle_property " + std::to_string(ti) +
                                     " defines a base material gradient");
                }
            }
        }

        if (has_components) {
            ValidateCoreCount(obj.components.size(), object_label + " components");
            for (std::size_t ci = 0; ci < obj.components.size(); ++ci) {
                if (!object_ids.contains(obj.components[ci].object_id)) {
                    throw InputError(object_label + " component " + std::to_string(ci) +
                                     " references non-existent object id " +
                                     std::to_string(obj.components[ci].object_id));
                }
                ValidateTransform(obj.components[ci].transform,
                                  object_label + " component " + std::to_string(ci) + " transform");
            }
        }

        if (obj.pid.has_value()) {
            auto it = material_group_sizes.find(*obj.pid);
            if (it == material_group_sizes.end()) {
                throw InputError(object_label + " references non-existent material group id " +
                                 std::to_string(*obj.pid));
            }
            if (obj.pindex.has_value() && *obj.pindex >= it->second) {
                throw InputError(object_label + " pindex " + std::to_string(*obj.pindex) +
                                 " out of range for material group " + std::to_string(*obj.pid) +
                                 " (size " + std::to_string(it->second) + ")");
            }
        }
    }

    {
        std::unordered_map<uint32_t, std::vector<uint32_t>> adj;
        std::unordered_map<uint32_t, std::string> id_to_name;
        for (const auto &obj : doc.objects) {
            id_to_name.emplace(obj.id, obj.name);
            if (!obj.components.empty()) {
                auto &children = adj[obj.id];
                for (const auto &comp : obj.components) { children.push_back(comp.object_id); }
            }
        }
        std::unordered_map<uint32_t, VisitState> visit_state;
        visit_state.reserve(doc.objects.size());
        for (const auto &obj : doc.objects) { visit_state[obj.id] = VisitState::Unvisited; }
        for (const auto &obj : doc.objects) {
            if (visit_state[obj.id] == VisitState::Unvisited) {
                DetectComponentCycles(obj.id, adj, visit_state, id_to_name);
            }
        }
    }

    if (doc.thumbnail.has_value()) {
        const auto &th = *doc.thumbnail;
        if (th.data.empty()) { throw InputError("Thumbnail data is empty"); }
        if (th.content_type != "image/png" && th.content_type != "image/jpeg") {
            throw InputError("Thumbnail content_type must be \"image/png\" or "
                             "\"image/jpeg\", got \"" +
                             th.content_type + "\"");
        }
    }
}

} // namespace neroued_3mf::detail
