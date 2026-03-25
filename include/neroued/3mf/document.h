// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file document.h
/// \brief 3MF Document model -- lightweight, non-owning mesh references.

#include "materials.h"
#include "types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace neroued_3mf {

enum class Unit { Micron, Millimeter, Centimeter, Inch, Foot, Meter };

enum class ObjectType { Model, SolidSupport, Support, Surface, Other };

struct Metadata {
    std::string name;
    std::string value;
    std::string type; ///< Optional type annotation (e.g. "xs:string"); empty = omit.
};

struct XmlNamespace {
    std::string prefix;
    std::string uri;
};

/// Intra-model component reference (3MF Core Spec 4.1.1).
struct Component {
    uint32_t object_id = 0;
    Transform transform = Transform::Identity();
};

struct Object {
    uint32_t id = 0;
    std::string name;
    ObjectType type = ObjectType::Model;
    std::string partnumber;

    MeshView mesh;
    /// Core-spec components (within same model file). Mutually exclusive with mesh.
    std::vector<Component> components;

    std::optional<uint32_t> pid;
    std::optional<uint32_t> pindex;

    std::vector<Metadata> metadata;

    /// Transform applied when this object is referenced as a Production Extension component.
    Transform component_transform = Transform::Identity();

    /// Production Extension p:UUID; empty = omit.
    std::string uuid;
};

struct BuildItem {
    uint32_t object_id = 0;
    Transform transform = Transform::Identity();
    std::string partnumber;
    /// Production Extension p:UUID; empty = omit.
    std::string uuid;
};

struct Thumbnail {
    std::vector<uint8_t> data;
    std::string content_type; ///< "image/png" or "image/jpeg"
};

// -- Generic extension injection points for vendor-specific content --

struct CustomPart {
    std::string path_in_zip;
    std::string content_type;
    std::vector<uint8_t> data;
};

struct CustomRelationship {
    std::string source_part; ///< e.g. "3D/3dmodel.model"; empty = package root
    std::string id;
    std::string type;
    std::string target;
};

struct CustomContentType {
    std::string extension;
    std::string content_type;
};

struct Document {
    Unit unit = Unit::Millimeter;
    std::string language = "en-US";
    std::vector<Metadata> metadata;
    std::vector<XmlNamespace> custom_namespaces;

    std::vector<BaseMaterialGroup> base_material_groups;
    std::vector<Object> objects;
    std::vector<BuildItem> build_items;

    std::optional<Thumbnail> thumbnail;

    std::vector<CustomPart> custom_parts;
    std::vector<CustomRelationship> custom_relationships;
    std::vector<CustomContentType> custom_content_types;

    struct ProductionConfig {
        bool enabled = false;
        Transform assembly_build_transform = Transform::Identity();
        bool merge_objects = false;
        std::vector<Metadata> external_model_metadata;
    } production;

    /// Mesh data owned by this Document (populated when DocumentBuilder
    /// receives Mesh&& via AddMeshObject). Objects' MeshView spans reference
    /// elements in this vector. Empty when all meshes are external (MeshView mode).
    std::vector<Mesh> owned_meshes;
};

} // namespace neroued_3mf
