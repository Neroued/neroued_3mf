// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

#include "neroued/3mf/document.h"
#include "neroued/3mf/error.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace neroued_3mf::detail {

enum class VisitState : uint8_t { Unvisited, Visiting, Visited };

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

inline void ValidateDocument(const Document &doc) {
    if (doc.objects.empty()) { throw InputError("Document has no objects"); }
    if (doc.build_items.empty()) { throw InputError("Document has no build items"); }

    std::unordered_set<uint32_t> object_ids;
    object_ids.reserve(doc.objects.size());
    for (const auto &obj : doc.objects) { object_ids.insert(obj.id); }

    std::unordered_map<uint32_t, std::size_t> material_group_sizes;
    material_group_sizes.reserve(doc.base_material_groups.size());
    for (const auto &bmg : doc.base_material_groups) {
        material_group_sizes.emplace(bmg.id, bmg.materials.size());
    }

    for (const auto &item : doc.build_items) {
        if (!object_ids.contains(item.object_id)) {
            throw InputError("BuildItem references non-existent object id " +
                             std::to_string(item.object_id));
        }
    }

    for (const auto &obj : doc.objects) {
        bool has_mesh = !obj.mesh.vertices.empty() || !obj.mesh.triangles.empty();
        bool has_components = !obj.components.empty();

        if (has_mesh && has_components) {
            throw InputError("Object \"" + obj.name + "\" (id " + std::to_string(obj.id) +
                             ") has both mesh and components; they are mutually exclusive");
        }
        if (!has_mesh && !has_components) {
            throw InputError("Object \"" + obj.name + "\" (id " + std::to_string(obj.id) +
                             ") has neither mesh nor components");
        }

        if (has_mesh) {
            if (obj.mesh.vertices.empty() || obj.mesh.triangles.empty()) {
                throw InputError("Object \"" + obj.name + "\" (id " + std::to_string(obj.id) +
                                 ") has an incomplete mesh (missing vertices or triangles)");
            }

            auto vcount = static_cast<uint32_t>(obj.mesh.vertices.size());
            for (std::size_t ti = 0; ti < obj.mesh.triangles.size(); ++ti) {
                const auto &tri = obj.mesh.triangles[ti];
                if (tri.v1 >= vcount || tri.v2 >= vcount || tri.v3 >= vcount) {
                    throw InputError("Object \"" + obj.name + "\" triangle " + std::to_string(ti) +
                                     " has out-of-range vertex index");
                }
            }

            if (!obj.mesh.triangle_properties.empty() &&
                obj.mesh.triangle_properties.size() != obj.mesh.triangles.size()) {
                throw InputError("Object \"" + obj.name + "\" triangle_properties size (" +
                                 std::to_string(obj.mesh.triangle_properties.size()) +
                                 ") != triangles size (" +
                                 std::to_string(obj.mesh.triangles.size()) + ")");
            }

            for (std::size_t ti = 0; ti < obj.mesh.triangle_properties.size(); ++ti) {
                const auto &tp = obj.mesh.triangle_properties[ti];
                if (material_group_sizes.find(tp.pid) == material_group_sizes.end()) {
                    throw InputError(
                        "Object \"" + obj.name + "\" triangle_property " + std::to_string(ti) +
                        " references non-existent material group " + std::to_string(tp.pid));
                }
            }
        }

        if (has_components) {
            for (std::size_t ci = 0; ci < obj.components.size(); ++ci) {
                if (!object_ids.contains(obj.components[ci].object_id)) {
                    throw InputError("Object \"" + obj.name + "\" component " + std::to_string(ci) +
                                     " references non-existent object id " +
                                     std::to_string(obj.components[ci].object_id));
                }
            }
        }

        if (obj.pid.has_value()) {
            auto it = material_group_sizes.find(*obj.pid);
            if (it == material_group_sizes.end()) {
                throw InputError("Object \"" + obj.name +
                                 "\" references non-existent material group id " +
                                 std::to_string(*obj.pid));
            }
            if (obj.pindex.has_value() && *obj.pindex >= it->second) {
                throw InputError("Object \"" + obj.name + "\" pindex " +
                                 std::to_string(*obj.pindex) + " out of range for material group " +
                                 std::to_string(*obj.pid) + " (size " + std::to_string(it->second) +
                                 ")");
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
