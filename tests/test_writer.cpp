#include "neroued/3mf/builder.h"
#include "neroued/3mf/error.h"
#include "neroued/3mf/writer.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace n3mf = neroued_3mf;

namespace {

n3mf::Document MakeSingleTriangleDocument() {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.SetUnit(n3mf::Unit::Millimeter);
    builder.AddMetadata("Application", "neroued_3mf_test");
    auto obj = builder.AddMeshObject("Triangle", std::move(mesh));
    builder.AddBuildItem(obj);
    return builder.Build();
}

n3mf::Document MakeMultiObjectDocument() {
    n3mf::DocumentBuilder builder;
    builder.SetUnit(n3mf::Unit::Millimeter);

    auto mat = builder.AddBaseMaterialGroup({
        {"Red", {255, 0, 0, 255}},
        {"Green", {0, 255, 0, 255}},
    });

    for (int i = 0; i < 3; ++i) {
        n3mf::Mesh m;
        float offset = static_cast<float>(i * 20);
        m.vertices = {{offset, 0, 0}, {offset + 10, 0, 0}, {offset + 5, 10, 0}};
        m.triangles = {{0, 1, 2}};
        auto obj = builder.AddMeshObject("Object_" + std::to_string(i), std::move(m), mat, i % 2);
        builder.AddBuildItem(obj);
    }
    return builder.Build();
}

bool HasZipMagic(const std::vector<uint8_t> &data) {
    return data.size() >= 4 && data[0] == 0x50 && data[1] == 0x4B && data[2] == 0x03 &&
           data[3] == 0x04;
}

bool ContainsSubstring(const std::vector<uint8_t> &data, const std::string &needle) {
    if (needle.empty() || data.size() < needle.size()) { return false; }
    auto it = std::search(data.begin(), data.end(), needle.begin(), needle.end());
    return it != data.end();
}

} // namespace

TEST(Writer, EmptyDocumentThrows) {
    n3mf::Document doc;
    EXPECT_THROW(n3mf::WriteToBuffer(doc), n3mf::InputError);
}

TEST(Writer, SingleTriangleBuffer) {
    auto doc = MakeSingleTriangleDocument();
    auto buf = n3mf::WriteToBuffer(doc);
    EXPECT_TRUE(HasZipMagic(buf));
    EXPECT_GT(buf.size(), 100u);
}

TEST(Writer, ContainsExpectedContent) {
    auto doc = MakeSingleTriangleDocument();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "3D/3dmodel.model"));
    EXPECT_TRUE(ContainsSubstring(buf, "[Content_Types].xml"));
    EXPECT_TRUE(ContainsSubstring(buf, "_rels/.rels"));
    EXPECT_TRUE(ContainsSubstring(buf, "<vertex"));
    EXPECT_TRUE(ContainsSubstring(buf, "<triangle"));
    EXPECT_TRUE(ContainsSubstring(buf, "neroued_3mf_test"));
}

TEST(Writer, MultiObjectWithMaterials) {
    auto doc = MakeMultiObjectDocument();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(HasZipMagic(buf));
    EXPECT_TRUE(ContainsSubstring(buf, "basematerials"));
    EXPECT_TRUE(ContainsSubstring(buf, "#FF0000"));
    EXPECT_TRUE(ContainsSubstring(buf, "#00FF00"));
    EXPECT_TRUE(ContainsSubstring(buf, "Object_0"));
    EXPECT_TRUE(ContainsSubstring(buf, "Object_1"));
    EXPECT_TRUE(ContainsSubstring(buf, "Object_2"));
}

TEST(Writer, DeterministicOutput) {
    auto doc = MakeSingleTriangleDocument();
    n3mf::WriteOptions opts;
    opts.deterministic = true;
    opts.compression = n3mf::WriteOptions::Compression::Store;

    auto buf1 = n3mf::WriteToBuffer(doc, opts);
    auto buf2 = n3mf::WriteToBuffer(doc, opts);
    EXPECT_EQ(buf1, buf2);
}

TEST(Writer, CustomPartsIncluded) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh.triangles = {{0, 1, 2}};

    std::string custom_data = "custom_data_here";
    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);
    builder.AddCustomPart({"Metadata/test.config",
                           "application/octet-stream",
                           {custom_data.begin(), custom_data.end()}});
    builder.AddCustomContentType({"config", "application/octet-stream"});

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "Metadata/test.config"));
    EXPECT_TRUE(ContainsSubstring(buf, "custom_data_here"));
}

TEST(Writer, WriteToFileAndReadBack) {
    auto doc = MakeSingleTriangleDocument();
    auto tmp_path = std::filesystem::temp_directory_path() / "neroued_3mf_test.3mf";

    n3mf::WriteToFile(tmp_path, doc);

    std::ifstream in(tmp_path, std::ios::binary);
    ASSERT_TRUE(in.is_open());
    std::vector<uint8_t> file_data((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
    in.close();

    EXPECT_TRUE(HasZipMagic(file_data));
    EXPECT_GT(file_data.size(), 100u);

    std::filesystem::remove(tmp_path);
}

TEST(Writer, EmptyPathThrows) {
    auto doc = MakeSingleTriangleDocument();
    EXPECT_THROW(n3mf::WriteToFile("", doc), n3mf::InputError);
}

TEST(Writer, ProductionModeCreatesExternalModels) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.EnableProduction(n3mf::Transform::Translation(128, 128, 0));

    n3mf::Mesh m1 = mesh, m2 = mesh;
    auto o1 = builder.AddMeshObject("Part1", std::move(m1));
    auto o2 = builder.AddMeshObject("Part2", std::move(m2));
    builder.AddBuildItem(o1);
    builder.AddBuildItem(o2);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(HasZipMagic(buf));
    EXPECT_TRUE(ContainsSubstring(buf, "3D/Objects/object_1.model"));
    EXPECT_TRUE(ContainsSubstring(buf, "3D/Objects/object_2.model"));
    EXPECT_TRUE(ContainsSubstring(buf, "components"));
    EXPECT_TRUE(ContainsSubstring(buf, "printable"));
}

TEST(Writer, ProductionCustomNamespacesInAssemblyXml) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.EnableProduction();
    builder.AddNamespace("BambuStudio", "http://schemas.bambulab.com/package/2021");
    builder.AddMetadata("BambuStudio:3mfVersion", "1");

    n3mf::Mesh m1 = mesh;
    auto o1 = builder.AddMeshObject("Part1", std::move(m1));
    builder.AddBuildItem(o1);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "xmlns:BambuStudio="));
    EXPECT_TRUE(ContainsSubstring(buf, "schemas.bambulab.com/package/2021"));
    EXPECT_TRUE(ContainsSubstring(buf, "BambuStudio:3mfVersion"));
}

TEST(Writer, ProductionMergedObjectsSingleFile) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.EnableProduction(n3mf::Transform::Translation(128, 128, 0))
        .SetProductionMergeObjects(true);
    builder.AddNamespace("BambuStudio", "http://schemas.bambulab.com/package/2021");
    builder.AddExternalModelMetadata("BambuStudio:3mfVersion", "1");

    n3mf::Mesh m1 = mesh, m2 = mesh, m3 = mesh;
    auto o1 = builder.AddMeshObject("Part1", std::move(m1));
    auto o2 = builder.AddMeshObject("Part2", std::move(m2));
    auto o3 = builder.AddMeshObject("Part3", std::move(m3));
    builder.AddBuildItem(o1);
    builder.AddBuildItem(o2);
    builder.AddBuildItem(o3);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(HasZipMagic(buf));
    EXPECT_TRUE(ContainsSubstring(buf, "3D/Objects/object_1.model"));
    EXPECT_FALSE(ContainsSubstring(buf, "3D/Objects/object_2.model"));
    EXPECT_FALSE(ContainsSubstring(buf, "3D/Objects/object_3.model"));
    EXPECT_TRUE(ContainsSubstring(buf, "components"));
    EXPECT_TRUE(ContainsSubstring(buf, "printable"));
    EXPECT_TRUE(ContainsSubstring(buf, "Part1"));
    EXPECT_TRUE(ContainsSubstring(buf, "Part2"));
    EXPECT_TRUE(ContainsSubstring(buf, "Part3"));
}

TEST(Writer, ExternalModelMetadataInObjectsModel) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.EnableProduction();
    builder.AddNamespace("p", "http://schemas.microsoft.com/3dmanufacturing/production/2015/06");
    builder.AddNamespace("BambuStudio", "http://schemas.bambulab.com/package/2021");
    builder.AddExternalModelMetadata("BambuStudio:3mfVersion", "1");

    n3mf::Mesh m1 = mesh;
    auto o1 = builder.AddMeshObject("Part", std::move(m1));
    builder.AddBuildItem(o1);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    std::string content(buf.begin(), buf.end());

    auto find_in_entry = [&](const std::string &entry_name, const std::string &needle) -> bool {
        auto pos = content.find(entry_name);
        if (pos == std::string::npos) return false;
        auto entry_start = content.find("<?xml", pos);
        if (entry_start == std::string::npos) return false;
        auto entry_end = content.find("</model>", entry_start);
        if (entry_end == std::string::npos) return false;
        std::string entry_content = content.substr(entry_start, entry_end - entry_start + 8);
        return entry_content.find(needle) != std::string::npos;
    };

    EXPECT_TRUE(find_in_entry("3D/Objects/object_1.model", "xmlns:BambuStudio="));
    EXPECT_TRUE(find_in_entry("3D/Objects/object_1.model", "BambuStudio:3mfVersion"));
}

TEST(Writer, TrianglePropertiesInOutput) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};
    mesh.triangle_properties = {{1, 0, 0, 1}};

    n3mf::DocumentBuilder builder;
    builder.AddBaseMaterialGroup({
        {"Red", {255, 0, 0, 255}},
        {"Blue", {0, 0, 255, 255}},
    });
    auto obj = builder.AddMeshObject("Colored", std::move(mesh));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "pid=\"1\""));
    EXPECT_TRUE(ContainsSubstring(buf, "p1=\"0\""));
    EXPECT_TRUE(ContainsSubstring(buf, "p3=\"1\""));
}

// -- New feature tests --

TEST(Writer, WriteToStream) {
    auto doc = MakeSingleTriangleDocument();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;

    std::ostringstream oss(std::ios::binary);
    n3mf::WriteToStream(oss, doc, opts);

    std::string str = oss.str();
    std::vector<uint8_t> buf(str.begin(), str.end());
    EXPECT_TRUE(HasZipMagic(buf));
    EXPECT_TRUE(ContainsSubstring(buf, "<vertex"));
}

TEST(Writer, ObjectTypeInOutput) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Support", std::move(mesh));
    builder.SetObjectType(obj, n3mf::ObjectType::SolidSupport);
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "type=\"solidsupport\""));
}

TEST(Writer, PartNumberInOutput) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.SetPartNumber(obj, "PN-42");
    builder.AddBuildItem(obj, n3mf::Transform::Identity(), "BUILD-PN");

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "partnumber=\"PN-42\""));
    EXPECT_TRUE(ContainsSubstring(buf, "partnumber=\"BUILD-PN\""));
}

TEST(Writer, PerObjectMetadataInOutput) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddObjectMetadata(obj, "ObjKey", "ObjVal", "xs:string");
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "<metadatagroup>"));
    EXPECT_TRUE(ContainsSubstring(buf, "ObjKey"));
    EXPECT_TRUE(ContainsSubstring(buf, "ObjVal"));
    EXPECT_TRUE(ContainsSubstring(buf, "xs:string"));
}

TEST(Writer, CoreComponentsInOutput) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto leaf = builder.AddMeshObject("Leaf", std::move(mesh));
    auto asm_id =
        builder.AddComponentObject("Assembly", {{leaf, n3mf::Transform::Translation(20, 0, 0)}});
    builder.AddBuildItem(asm_id);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "<components>"));
    EXPECT_TRUE(ContainsSubstring(buf, "objectid=\""));
    EXPECT_TRUE(ContainsSubstring(buf, "transform=\""));
}

TEST(Writer, ThumbnailInOutput) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    std::vector<uint8_t> fake_png = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    n3mf::DocumentBuilder builder;
    builder.SetThumbnail(fake_png, "image/png");
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(HasZipMagic(buf));
    EXPECT_TRUE(ContainsSubstring(buf, "Metadata/thumbnail.png"));
    EXPECT_TRUE(ContainsSubstring(buf, "image/png"));
}

TEST(Writer, MetadataTypeInOutput) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.AddMetadata("Title", "TestDoc", "xs:string");
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "type=\"xs:string\""));
}

TEST(Writer, DocumentLevelNamespacesInFlatModel) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.AddNamespace("vendor", "http://vendor.example.com/ns");
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "xmlns:vendor=\"http://vendor.example.com/ns\""));
}

TEST(Writer, LanguageInOutput) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.SetLanguage("zh-CN");
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "xml:lang=\"zh-CN\""));
}

TEST(Writer, ComponentTransformInProduction) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.EnableProduction();
    n3mf::Mesh m1 = mesh;
    auto obj = builder.AddMeshObject("Part", std::move(m1));
    builder.SetComponentTransform(obj, n3mf::Transform::Translation(50, 50, 0));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "transform=\""));
    EXPECT_TRUE(ContainsSubstring(buf, "p:path="));
}

TEST(Writer, BinaryCustomPartData) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    std::vector<uint8_t> binary_data = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);
    builder.AddCustomPart({"Metadata/binary.bin", "application/octet-stream", binary_data});
    builder.AddCustomContentType({"bin", "application/octet-stream"});

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(HasZipMagic(buf));
    EXPECT_TRUE(ContainsSubstring(buf, "Metadata/binary.bin"));
}

TEST(Writer, SurfaceObjectTypeInOutput) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("SurfacePart", std::move(mesh));
    builder.SetObjectType(obj, n3mf::ObjectType::Surface);
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "type=\"surface\""));
}

TEST(Writer, ProductionUUIDInOutput) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.EnableProduction();
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.SetObjectUUID(obj, "550e8400-e29b-41d4-a716-446655440000");
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "p:UUID=\"550e8400-e29b-41d4-a716-446655440000\""));
}

TEST(Writer, BuildItemUUIDInFlatModel) {
    n3mf::Mesh mesh;
    mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    builder.AddNamespace("p", "http://schemas.microsoft.com/3dmanufacturing/production/2015/06");
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj, n3mf::Transform::Identity(), "", "item-uuid-5678");

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(ContainsSubstring(buf, "p:UUID=\"item-uuid-5678\""));
}
