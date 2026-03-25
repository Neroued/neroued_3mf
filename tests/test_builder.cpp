#include "neroued/3mf/builder.h"
#include "neroued/3mf/error.h"
#include "neroued/3mf/writer.h"

#include <gtest/gtest.h>

namespace n3mf = neroued_3mf;

TEST(DocumentBuilder, BasicBuild) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.SetUnit(n3mf::Unit::Millimeter);
    builder.AddMetadata("Application", "TestApp");

    auto mat_group = builder.AddBaseMaterialGroup({
        {"Red", {255, 0, 0, 255}},
        {"Blue", {0, 0, 255, 255}},
    });
    EXPECT_EQ(mat_group, 1u);

    auto obj_id = builder.AddMeshObject("TestMesh", mesh.View(), mat_group, 0);
    EXPECT_EQ(obj_id, 2u);

    builder.AddBuildItem(obj_id);

    auto doc = builder.Build();
    EXPECT_EQ(doc.unit, n3mf::Unit::Millimeter);
    EXPECT_EQ(doc.metadata.size(), 1u);
    EXPECT_EQ(doc.base_material_groups.size(), 1u);
    EXPECT_EQ(doc.objects.size(), 1u);
    EXPECT_EQ(doc.build_items.size(), 1u);
    EXPECT_EQ(doc.objects[0].id, obj_id);
    EXPECT_EQ(doc.objects[0].mesh.vertices.size(), 3u);
    EXPECT_EQ(*doc.objects[0].pid, mat_group);
    EXPECT_EQ(*doc.objects[0].pindex, 0u);
}

TEST(DocumentBuilder, OwnedMesh) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj_id = builder.AddMeshObject("Owned", std::move(mesh));
    builder.AddBuildItem(obj_id);

    auto doc = builder.Build();
    EXPECT_EQ(doc.objects.size(), 1u);
    EXPECT_EQ(doc.objects[0].mesh.vertices.size(), 3u);
}

TEST(DocumentBuilder, AutoIncrementIds) {
    n3mf::Mesh m1, m2;
    m1.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    m1.triangles = {{0, 1, 2}};
    m2 = m1;

    n3mf::DocumentBuilder builder;
    auto id1 = builder.AddMeshObject("A", std::move(m1));
    auto id2 = builder.AddMeshObject("B", std::move(m2));
    EXPECT_NE(id1, id2);
    EXPECT_EQ(id1, 1u);
    EXPECT_EQ(id2, 2u);
}

TEST(DocumentBuilder, MaterialGroupAutoId) {
    n3mf::DocumentBuilder builder;
    auto g1 = builder.AddBaseMaterialGroup({{"White", {255, 255, 255, 255}}});
    auto g2 = builder.AddBaseMaterialGroup({{"Black", {0, 0, 0, 255}}});
    EXPECT_NE(g1, g2);
}

TEST(DocumentBuilder, EmptyMaterialGroupThrows) {
    n3mf::DocumentBuilder builder;
    EXPECT_THROW(builder.AddBaseMaterialGroup({}), n3mf::InputError);
}

TEST(DocumentBuilder, CustomParts) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    std::string config_data = "<config/>";
    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Test", mesh.View());
    builder.AddBuildItem(obj);
    builder.AddCustomPart({"Metadata/settings.config",
                           "application/octet-stream",
                           {config_data.begin(), config_data.end()}});
    builder.AddCustomRelationship(
        {"3D/3dmodel.model", "rel-custom", "http://example.com/type", "/Metadata/settings.config"});
    builder.AddCustomContentType({"config", "application/octet-stream"});

    auto doc = builder.Build();
    EXPECT_EQ(doc.custom_parts.size(), 1u);
    EXPECT_EQ(doc.custom_relationships.size(), 1u);
    EXPECT_EQ(doc.custom_content_types.size(), 1u);
}

TEST(DocumentBuilder, ProductionMode) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.EnableProduction(n3mf::Transform::Translation(128, 128, 0));
    auto obj = builder.AddMeshObject("Part", mesh.View());
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    EXPECT_TRUE(doc.production.enabled);
    EXPECT_FLOAT_EQ(doc.production.assembly_build_transform.m[9], 128.0f);
}

TEST(DocumentBuilder, ProductionNamespacesAndMetadata) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.EnableProduction().SetProductionMergeObjects(true);
    builder.AddNamespace("BambuStudio", "http://schemas.bambulab.com/package/2021");
    builder.AddExternalModelMetadata("BambuStudio:3mfVersion", "1");
    auto obj = builder.AddMeshObject("Part", mesh.View());
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    EXPECT_TRUE(doc.production.enabled);
    EXPECT_TRUE(doc.production.merge_objects);
    ASSERT_EQ(doc.custom_namespaces.size(), 2u);
    EXPECT_EQ(doc.custom_namespaces[0].prefix, "p");
    EXPECT_EQ(doc.custom_namespaces[1].prefix, "BambuStudio");
    EXPECT_EQ(doc.custom_namespaces[1].uri, "http://schemas.bambulab.com/package/2021");
    ASSERT_EQ(doc.production.external_model_metadata.size(), 1u);
    EXPECT_EQ(doc.production.external_model_metadata[0].name, "BambuStudio:3mfVersion");
    EXPECT_EQ(doc.production.external_model_metadata[0].value, "1");
}

// -- New API tests --

TEST(DocumentBuilder, SetLanguage) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.SetLanguage("zh-CN");
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    EXPECT_EQ(doc.language, "zh-CN");
}

TEST(DocumentBuilder, MetadataType) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.AddMetadata("Title", "Test", "xs:string");
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    ASSERT_EQ(doc.metadata.size(), 1u);
    EXPECT_EQ(doc.metadata[0].type, "xs:string");
}

TEST(DocumentBuilder, ObjectTypeAndPartNumber) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Support", std::move(mesh));
    builder.SetObjectType(obj, n3mf::ObjectType::SolidSupport);
    builder.SetPartNumber(obj, "PART-001");
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    EXPECT_EQ(doc.objects[0].type, n3mf::ObjectType::SolidSupport);
    EXPECT_EQ(doc.objects[0].partnumber, "PART-001");
}

TEST(DocumentBuilder, BuildItemPartNumber) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj, n3mf::Transform::Identity(), "BUILD-001");

    auto doc = builder.Build();
    EXPECT_EQ(doc.build_items[0].partnumber, "BUILD-001");
}

TEST(DocumentBuilder, PerObjectMetadata) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddObjectMetadata(obj, "CustomData", "value123", "xs:string");
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    ASSERT_EQ(doc.objects[0].metadata.size(), 1u);
    EXPECT_EQ(doc.objects[0].metadata[0].name, "CustomData");
    EXPECT_EQ(doc.objects[0].metadata[0].value, "value123");
    EXPECT_EQ(doc.objects[0].metadata[0].type, "xs:string");
}

TEST(DocumentBuilder, ComponentObject) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto part_id = builder.AddMeshObject("Leaf", std::move(mesh));
    auto asm_id =
        builder.AddComponentObject("Assembly", {{part_id, n3mf::Transform::Translation(10, 0, 0)}});
    builder.AddBuildItem(asm_id);

    auto doc = builder.Build();
    ASSERT_EQ(doc.objects.size(), 2u);
    EXPECT_FALSE(doc.objects[1].components.empty());
    EXPECT_EQ(doc.objects[1].components[0].object_id, part_id);
}

TEST(DocumentBuilder, ComponentTransform) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.SetComponentTransform(obj, n3mf::Transform::Translation(5, 10, 15));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    EXPECT_FLOAT_EQ(doc.objects[0].component_transform.m[9], 5.0f);
    EXPECT_FLOAT_EQ(doc.objects[0].component_transform.m[10], 10.0f);
    EXPECT_FLOAT_EQ(doc.objects[0].component_transform.m[11], 15.0f);
}

TEST(DocumentBuilder, Thumbnail) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.SetThumbnail({0x89, 'P', 'N', 'G'}, "image/png");
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    ASSERT_TRUE(doc.thumbnail.has_value());
    EXPECT_EQ(doc.thumbnail->content_type, "image/png");
    EXPECT_EQ(doc.thumbnail->data.size(), 4u);
}

TEST(DocumentBuilder, DocumentLevelNamespaces) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.AddNamespace("custom", "http://example.com/ns");
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    ASSERT_EQ(doc.custom_namespaces.size(), 1u);
    EXPECT_EQ(doc.custom_namespaces[0].prefix, "custom");
}

// -- Validation tests -------------------------------------------------------

TEST(DocumentBuilder, NoBuildItemsThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.AddMeshObject("Part", std::move(mesh));
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, InvalidBuildItemObjectIdThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(999);
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, InvalidPidThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh), /*pid=*/999, /*pindex=*/0);
    builder.AddBuildItem(obj);
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, PindexOutOfRangeThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto mat = builder.AddBaseMaterialGroup({{"Red", {255, 0, 0, 255}}});
    auto obj = builder.AddMeshObject("Part", std::move(mesh), mat, /*pindex=*/5);
    builder.AddBuildItem(obj);
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, TrianglePropertiesSizeMismatchThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};
    mesh.triangle_properties = {{1, 0, 0, 0}, {1, 0, 0, 0}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, EmptyMeshThrows) {
    n3mf::Mesh mesh;

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Empty", std::move(mesh));
    builder.AddBuildItem(obj);
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, VertexIndexOutOfRangeThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 99}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Bad", std::move(mesh));
    builder.AddBuildItem(obj);
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, MeshAndComponentsMutuallyExclusive) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto leaf = builder.AddMeshObject("Leaf", std::move(mesh));

    n3mf::Document doc;
    doc.objects.push_back(n3mf::Object{});
    auto &obj = doc.objects.back();
    obj.id = 10;
    obj.name = "Both";
    n3mf::Mesh m2;
    m2.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    m2.triangles = {{0, 1, 2}};
    obj.mesh = m2.View();
    obj.components = {{leaf}};
    doc.objects.push_back(n3mf::Object{.id = leaf, .name = "Leaf"});
    doc.objects.back().mesh = m2.View();
    doc.build_items.push_back({10});
    doc.owned_meshes.push_back(std::move(m2));

    EXPECT_THROW(n3mf::WriteToBuffer(doc), n3mf::InputError);
}

TEST(DocumentBuilder, ComponentReferencesNonExistentObjectThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.AddMeshObject("Leaf", std::move(mesh));
    auto asm_id = builder.AddComponentObject("Bad", {{999}});
    builder.AddBuildItem(asm_id);
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, EmptyComponentObjectThrows) {
    n3mf::DocumentBuilder builder;
    EXPECT_THROW(builder.AddComponentObject("Bad", {}), n3mf::InputError);
}

TEST(DocumentBuilder, InvalidThumbnailContentTypeThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.SetThumbnail({1, 2, 3}, "image/gif");
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, EmptyThumbnailDataThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.SetThumbnail({}, "image/png");
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, SelfReferencingComponentThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto leaf = builder.AddMeshObject("Leaf", std::move(mesh));

    n3mf::Document doc;
    n3mf::Mesh m2;
    m2.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    m2.triangles = {{0, 1, 2}};

    n3mf::Object leaf_obj;
    leaf_obj.id = 1;
    leaf_obj.name = "Leaf";
    leaf_obj.mesh = m2.View();

    n3mf::Object self_ref;
    self_ref.id = 2;
    self_ref.name = "SelfRef";
    self_ref.components = {{2}};

    doc.objects = {leaf_obj, self_ref};
    doc.build_items = {{2}};
    doc.owned_meshes.push_back(std::move(m2));

    EXPECT_THROW(n3mf::WriteToBuffer(doc), n3mf::InputError);
}

TEST(DocumentBuilder, InvalidObjectIdInSettersThrows) {
    n3mf::DocumentBuilder builder;
    EXPECT_THROW(builder.SetObjectType(999, n3mf::ObjectType::Model), n3mf::InputError);
    EXPECT_THROW(builder.SetPartNumber(999, "x"), n3mf::InputError);
    EXPECT_THROW(builder.AddObjectMetadata(999, "k", "v"), n3mf::InputError);
    EXPECT_THROW(builder.SetComponentTransform(999, n3mf::Transform::Identity()), n3mf::InputError);
}

TEST(DocumentBuilder, TrianglePropertyInvalidPidThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};
    mesh.triangle_properties = {{999, 0, 0, 0}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, IndirectCircularComponentThrows) {
    n3mf::Mesh m1;
    m1.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    m1.triangles = {{0, 1, 2}};

    n3mf::Document doc;
    n3mf::Object leaf;
    leaf.id = 1;
    leaf.name = "Leaf";
    leaf.mesh = m1.View();

    n3mf::Object a;
    a.id = 2;
    a.name = "A";
    a.components = {{3}};

    n3mf::Object b;
    b.id = 3;
    b.name = "B";
    b.components = {{2}};

    doc.objects = {leaf, a, b};
    doc.build_items = {{2}};
    doc.owned_meshes.push_back(std::move(m1));

    EXPECT_THROW(n3mf::WriteToBuffer(doc), n3mf::InputError);
}

TEST(DocumentBuilder, BuildCalledTwiceThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);
    auto doc = builder.Build();
    EXPECT_THROW(builder.Build(), n3mf::InputError);
}

TEST(DocumentBuilder, AddAfterBuildThrows) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);
    auto doc = builder.Build();

    n3mf::Mesh mesh2;
    mesh2.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh2.triangles = {{0, 1, 2}};
    EXPECT_THROW(builder.AddMeshObject("Another", std::move(mesh2)), n3mf::InputError);
    EXPECT_THROW(builder.AddBuildItem(obj), n3mf::InputError);
    EXPECT_THROW(builder.SetUnit(n3mf::Unit::Inch), n3mf::InputError);
    EXPECT_THROW(builder.AddMetadata("k", "v"), n3mf::InputError);
}

TEST(DocumentBuilder, ObjectUUID) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.SetObjectUUID(obj, "550e8400-e29b-41d4-a716-446655440000");
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    EXPECT_EQ(doc.objects[0].uuid, "550e8400-e29b-41d4-a716-446655440000");
}

TEST(DocumentBuilder, BuildItemUUID) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj, n3mf::Transform::Identity(), "", "item-uuid-1234");

    auto doc = builder.Build();
    EXPECT_EQ(doc.build_items[0].uuid, "item-uuid-1234");
}
