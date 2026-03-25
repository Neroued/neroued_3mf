// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#include "neroued/3mf/builder.h"

#include "neroued/3mf/error.h"

#include "internal/validate.h"

#include <algorithm>
#include <utility>

namespace neroued_3mf {

void DocumentBuilder::CheckNotBuilt() const {
    if (built_) { throw InputError("Builder already consumed by Build()"); }
}

Object &DocumentBuilder::FindObjectById(uint32_t id) {
    auto it = object_id_index_.find(id);
    if (it == object_id_index_.end()) {
        throw InputError("No object with id " + std::to_string(id));
    }
    return objects_[it->second];
}

uint32_t DocumentBuilder::NextId() {
    return next_id_++;
}

DocumentBuilder &DocumentBuilder::SetUnit(Unit unit) {
    CheckNotBuilt();
    unit_ = unit;
    return *this;
}

DocumentBuilder &DocumentBuilder::SetLanguage(std::string lang) {
    CheckNotBuilt();
    language_ = std::move(lang);
    return *this;
}

DocumentBuilder &DocumentBuilder::AddMetadata(std::string name, std::string value,
                                              std::string type) {
    CheckNotBuilt();
    metadata_.push_back({std::move(name), std::move(value), std::move(type)});
    return *this;
}

DocumentBuilder &DocumentBuilder::AddNamespace(std::string prefix, std::string uri) {
    CheckNotBuilt();
    for (auto &ns : custom_namespaces_) {
        if (ns.prefix == prefix) {
            ns.uri = std::move(uri);
            return *this;
        }
    }
    custom_namespaces_.push_back({std::move(prefix), std::move(uri)});
    return *this;
}

uint32_t DocumentBuilder::AddBaseMaterialGroup(std::vector<BaseMaterial> materials) {
    CheckNotBuilt();
    if (materials.empty()) { throw InputError("BaseMaterialGroup must not be empty"); }
    uint32_t id = NextId();
    base_material_groups_.push_back({id, std::move(materials)});
    return id;
}

uint32_t DocumentBuilder::AddMeshObject(std::string name, MeshView mesh,
                                        std::optional<uint32_t> pid,
                                        std::optional<uint32_t> pindex) {
    CheckNotBuilt();
    uint32_t id = NextId();
    Object obj;
    obj.id = id;
    obj.name = std::move(name);
    obj.mesh = mesh;
    obj.pid = pid;
    obj.pindex = pindex;
    objects_.push_back(std::move(obj));
    object_id_index_.emplace(id, objects_.size() - 1);
    return id;
}

uint32_t DocumentBuilder::AddMeshObject(std::string name, Mesh &&mesh, std::optional<uint32_t> pid,
                                        std::optional<uint32_t> pindex) {
    CheckNotBuilt();
    owned_meshes_.push_back(std::move(mesh));
    MeshView view = owned_meshes_.back().View();
    uint32_t id = NextId();
    Object obj;
    obj.id = id;
    obj.name = std::move(name);
    obj.mesh = view;
    obj.pid = pid;
    obj.pindex = pindex;
    objects_.push_back(std::move(obj));
    object_id_index_.emplace(id, objects_.size() - 1);
    return id;
}

uint32_t DocumentBuilder::AddComponentObject(std::string name, std::vector<Component> components) {
    CheckNotBuilt();
    if (components.empty()) {
        throw InputError("Component object must have at least one component");
    }
    uint32_t id = NextId();
    Object obj;
    obj.id = id;
    obj.name = std::move(name);
    obj.components = std::move(components);
    objects_.push_back(std::move(obj));
    object_id_index_.emplace(id, objects_.size() - 1);
    return id;
}

DocumentBuilder &DocumentBuilder::SetObjectType(uint32_t object_id, ObjectType type) {
    CheckNotBuilt();
    FindObjectById(object_id).type = type;
    return *this;
}

DocumentBuilder &DocumentBuilder::SetPartNumber(uint32_t object_id, std::string partnumber) {
    CheckNotBuilt();
    FindObjectById(object_id).partnumber = std::move(partnumber);
    return *this;
}

DocumentBuilder &DocumentBuilder::AddObjectMetadata(uint32_t object_id, std::string name,
                                                    std::string value, std::string type) {
    CheckNotBuilt();
    FindObjectById(object_id).metadata.push_back(
        {std::move(name), std::move(value), std::move(type)});
    return *this;
}

DocumentBuilder &DocumentBuilder::SetComponentTransform(uint32_t object_id, Transform transform) {
    CheckNotBuilt();
    FindObjectById(object_id).component_transform = transform;
    return *this;
}

DocumentBuilder &DocumentBuilder::SetObjectUUID(uint32_t object_id, std::string uuid) {
    CheckNotBuilt();
    FindObjectById(object_id).uuid = std::move(uuid);
    return *this;
}

DocumentBuilder &DocumentBuilder::AddBuildItem(uint32_t object_id, Transform transform,
                                               std::string partnumber, std::string uuid) {
    CheckNotBuilt();
    build_items_.push_back({object_id, transform, std::move(partnumber), std::move(uuid)});
    return *this;
}

DocumentBuilder &DocumentBuilder::SetThumbnail(std::vector<uint8_t> data,
                                               std::string content_type) {
    CheckNotBuilt();
    thumbnail_ = Thumbnail{std::move(data), std::move(content_type)};
    return *this;
}

DocumentBuilder &DocumentBuilder::AddCustomPart(CustomPart part) {
    CheckNotBuilt();
    custom_parts_.push_back(std::move(part));
    return *this;
}

DocumentBuilder &DocumentBuilder::AddCustomRelationship(CustomRelationship rel) {
    CheckNotBuilt();
    custom_relationships_.push_back(std::move(rel));
    return *this;
}

DocumentBuilder &DocumentBuilder::AddCustomContentType(CustomContentType ct) {
    CheckNotBuilt();
    custom_content_types_.push_back(std::move(ct));
    return *this;
}

DocumentBuilder &DocumentBuilder::EnableProduction(Transform assembly_transform) {
    CheckNotBuilt();
    production_.enabled = true;
    production_.assembly_build_transform = assembly_transform;
    bool has_p = std::any_of(custom_namespaces_.begin(), custom_namespaces_.end(),
                             [](const XmlNamespace &ns) { return ns.prefix == "p"; });
    if (!has_p) {
        custom_namespaces_.push_back(
            {"p", "http://schemas.microsoft.com/3dmanufacturing/production/2015/06"});
    }
    return *this;
}

DocumentBuilder &DocumentBuilder::SetProductionMergeObjects(bool merge) {
    CheckNotBuilt();
    production_.merge_objects = merge;
    return *this;
}

DocumentBuilder &DocumentBuilder::AddExternalModelMetadata(std::string name, std::string value,
                                                           std::string type) {
    CheckNotBuilt();
    production_.external_model_metadata.push_back(
        {std::move(name), std::move(value), std::move(type)});
    return *this;
}

Document DocumentBuilder::Build() {
    CheckNotBuilt();
    built_ = true;

    Document doc;
    doc.unit = unit_;
    doc.language = std::move(language_);
    doc.metadata = std::move(metadata_);
    doc.custom_namespaces = std::move(custom_namespaces_);
    doc.base_material_groups = std::move(base_material_groups_);
    doc.objects = std::move(objects_);
    doc.build_items = std::move(build_items_);
    doc.thumbnail = std::move(thumbnail_);
    doc.custom_parts = std::move(custom_parts_);
    doc.custom_relationships = std::move(custom_relationships_);
    doc.custom_content_types = std::move(custom_content_types_);
    doc.production = std::move(production_);
    // std::vector move preserves element addresses, so existing
    // MeshView spans referencing owned_meshes_ remain valid.
    doc.owned_meshes = std::move(owned_meshes_);

    detail::ValidateDocument(doc);
    return doc;
}

} // namespace neroued_3mf
