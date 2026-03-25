// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file builder.h
/// \brief DocumentBuilder -- type-safe construction of a 3MF Document.

#include "document.h"
#include "materials.h"
#include "types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace neroued_3mf {

class DocumentBuilder {
  public:
    DocumentBuilder &SetUnit(Unit unit);
    DocumentBuilder &SetLanguage(std::string lang);
    DocumentBuilder &AddMetadata(std::string name, std::string value, std::string type = "");

    /// Add an XML namespace declaration to all model root elements.
    DocumentBuilder &AddNamespace(std::string prefix, std::string uri);

    /// Add a base material group. Returns the assigned group ID.
    uint32_t AddBaseMaterialGroup(std::vector<BaseMaterial> materials);

    /// Add a mesh object from a non-owning MeshView.
    /// Caller must keep the referenced data alive until Write returns.
    uint32_t AddMeshObject(std::string name, MeshView mesh, std::optional<uint32_t> pid = {},
                           std::optional<uint32_t> pindex = {});

    /// Add a mesh object by transferring ownership. Builder keeps it alive.
    uint32_t AddMeshObject(std::string name, Mesh &&mesh, std::optional<uint32_t> pid = {},
                           std::optional<uint32_t> pindex = {});

    /// Add a component object (core-spec assembly within a single model file).
    uint32_t AddComponentObject(std::string name, std::vector<Component> components);

    DocumentBuilder &SetObjectType(uint32_t object_id, ObjectType type);
    DocumentBuilder &SetPartNumber(uint32_t object_id, std::string partnumber);
    DocumentBuilder &AddObjectMetadata(uint32_t object_id, std::string name, std::string value,
                                       std::string type = "");
    DocumentBuilder &SetComponentTransform(uint32_t object_id, Transform transform);
    DocumentBuilder &SetObjectUUID(uint32_t object_id, std::string uuid);

    DocumentBuilder &AddBuildItem(uint32_t object_id, Transform transform = Transform::Identity(),
                                  std::string partnumber = "", std::string uuid = "");

    DocumentBuilder &SetThumbnail(std::vector<uint8_t> data, std::string content_type);

    DocumentBuilder &AddCustomPart(CustomPart part);
    DocumentBuilder &AddCustomRelationship(CustomRelationship rel);
    DocumentBuilder &AddCustomContentType(CustomContentType ct);

    DocumentBuilder &EnableProduction(Transform assembly_transform = Transform::Identity());
    DocumentBuilder &SetProductionMergeObjects(bool merge = true);
    DocumentBuilder &AddExternalModelMetadata(std::string name, std::string value,
                                              std::string type = "");

    /// Build the Document and transfer ownership of any owned meshes.
    /// After calling Build(), the builder is in a moved-from state for owned meshes.
    Document Build();

  private:
    uint32_t NextId();
    void CheckNotBuilt() const;
    Object &FindObjectById(uint32_t id);

    bool built_ = false;
    Unit unit_ = Unit::Millimeter;
    std::string language_ = "en-US";
    std::vector<Metadata> metadata_;
    std::vector<XmlNamespace> custom_namespaces_;
    std::vector<BaseMaterialGroup> base_material_groups_;
    std::vector<Object> objects_;
    std::vector<BuildItem> build_items_;

    std::optional<Thumbnail> thumbnail_;

    std::vector<CustomPart> custom_parts_;
    std::vector<CustomRelationship> custom_relationships_;
    std::vector<CustomContentType> custom_content_types_;
    Document::ProductionConfig production_;

    std::vector<Mesh> owned_meshes_;
    std::unordered_map<uint32_t, std::size_t> object_id_index_;
    uint32_t next_id_ = 1;
};

} // namespace neroued_3mf
