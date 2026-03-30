#include "neroued/3mf/builder.h"
#include "neroued/3mf/error.h"
#include "neroued/3mf/watermark.h"
#include "neroued/3mf/writer.h"

#include "internal/sha256.h"
#include "internal/watermark.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
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

TEST(Writer, VertexPrecisionReducesSize) {
    n3mf::Mesh mesh;
    mesh.vertices = {{1.23456789f, 2.34567890f, 3.45678901f},
                     {10.1234567f, 0.00123456f, 5.67890123f},
                     {5.55555555f, 10.1010101f, 0.99999999f}};
    mesh.triangles = {{0, 1, 2}};

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("Part", std::move(mesh));
    builder.AddBuildItem(obj);
    auto doc = builder.Build();

    n3mf::WriteOptions opts_full;
    opts_full.compression = n3mf::WriteOptions::Compression::Store;
    opts_full.vertex_precision = 9;
    auto buf_full = n3mf::WriteToBuffer(doc, opts_full);

    n3mf::WriteOptions opts_low;
    opts_low.compression = n3mf::WriteOptions::Compression::Store;
    opts_low.vertex_precision = 3;
    auto buf_low = n3mf::WriteToBuffer(doc, opts_low);

    EXPECT_LT(buf_low.size(), buf_full.size());
    EXPECT_GT(buf_low.size(), 100u);
    EXPECT_EQ(buf_low[0], 0x50);
    EXPECT_EQ(buf_low[1], 0x4B);
}

TEST(Writer, VertexPrecisionDefault) {
    n3mf::WriteOptions opts;
    EXPECT_EQ(opts.vertex_precision, 9);
}

// -- SHA256 / HMAC-SHA256 tests --

TEST(SHA256, EmptyInput) {
    n3mf::detail::SHA256 h;
    auto digest = h.Final();
    // SHA-256("") = e3b0c442...
    EXPECT_EQ(digest[0], 0xe3);
    EXPECT_EQ(digest[1], 0xb0);
    EXPECT_EQ(digest[2], 0xc4);
    EXPECT_EQ(digest[3], 0x42);
    EXPECT_EQ(digest[31], 0x55);
}

TEST(SHA256, Abc) {
    n3mf::detail::SHA256 h;
    const uint8_t msg[] = {'a', 'b', 'c'};
    h.Update(msg, 3);
    auto digest = h.Final();
    // SHA-256("abc") = ba7816bf 8f01cfea ...
    EXPECT_EQ(digest[0], 0xba);
    EXPECT_EQ(digest[1], 0x78);
    EXPECT_EQ(digest[2], 0x16);
    EXPECT_EQ(digest[3], 0xbf);
}

TEST(HMAC_SHA256, KnownVector) {
    // RFC 4231 test case 2: key="Jefe", data="what do ya want for nothing?"
    const uint8_t key[] = {'J', 'e', 'f', 'e'};
    const char *msg = "what do ya want for nothing?";
    auto mac =
        n3mf::detail::HmacSHA256(key, 4, reinterpret_cast<const uint8_t *>(msg), std::strlen(msg));
    EXPECT_EQ(mac[0], 0x5b);
    EXPECT_EQ(mac[1], 0xdc);
    EXPECT_EQ(mac[2], 0xc1);
    EXPECT_EQ(mac[3], 0x46);
}

// -- Watermark round-trip tests --

TEST(Watermark, RoundTripBasic) {
    std::vector<uint8_t> payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};

    auto table = n3mf::detail::BuildRotationTable(payload, key, 3, 10000);
    ASSERT_FALSE(table.empty());
    EXPECT_EQ(table.size(), 10000u);

    for (uint8_t v : table) { EXPECT_LE(v, 1u); }

    auto decoded = n3mf::detail::DecodePayload(key, table);
    EXPECT_EQ(decoded, payload);
}

TEST(Watermark, RoundTripNoKey) {
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    std::vector<uint8_t> key; // empty = no encryption

    auto table = n3mf::detail::BuildRotationTable(payload, key, 3, 5000);
    ASSERT_FALSE(table.empty());

    auto decoded = n3mf::detail::DecodePayload(key, table);
    EXPECT_EQ(decoded, payload);
}

TEST(Watermark, DegradationTo1x) {
    std::vector<uint8_t> payload(20, 0xAB);
    std::vector<uint8_t> key = {0xFF};
    // 20 + 11 = 31 bytes = 248 bits. With 3x rep = 744 bits needed, but only 500 available.
    // Should degrade to 1x (248 bits < 500).
    auto table = n3mf::detail::BuildRotationTable(payload, key, 3, 500);
    ASSERT_FALSE(table.empty());

    auto decoded = n3mf::detail::DecodePayload(key, table);
    EXPECT_EQ(decoded, payload);
}

TEST(Watermark, TruncationWhenTooFew) {
    std::vector<uint8_t> payload(100, 0x42);
    std::vector<uint8_t> key = {0x01};
    // 100 + 11 = 111 bytes = 888 bits at 1x. Only 200 triangles = 200 bits available.
    // header 11 bytes = 88 bits → payload 200/8 - 11 = 14 bytes (truncated).
    auto table = n3mf::detail::BuildRotationTable(payload, key, 1, 200);
    ASSERT_FALSE(table.empty());

    auto decoded = n3mf::detail::DecodePayload(key, table);
    ASSERT_FALSE(decoded.empty());
    EXPECT_LT(decoded.size(), payload.size());
    EXPECT_EQ(std::vector<uint8_t>(decoded.begin(), decoded.end()),
              std::vector<uint8_t>(payload.begin(),
                                   payload.begin() + static_cast<std::ptrdiff_t>(decoded.size())));
}

TEST(Watermark, TooFewTrianglesReturnsEmpty) {
    std::vector<uint8_t> payload = {0x01};
    std::vector<uint8_t> key = {0x01};
    // 11 bytes overhead = 88 bits, but only 50 triangles.
    auto table = n3mf::detail::BuildRotationTable(payload, key, 1, 50);
    EXPECT_TRUE(table.empty());
}

TEST(Watermark, EmptyPayloadReturnsEmpty) {
    std::vector<uint8_t> payload;
    std::vector<uint8_t> key = {0x01};
    auto table = n3mf::detail::BuildRotationTable(payload, key, 3, 10000);
    EXPECT_TRUE(table.empty());
}

// -- Writer integration tests with watermark --

namespace {
n3mf::Document MakeLargeMeshDocument(std::size_t num_triangles) {
    n3mf::Mesh mesh;
    mesh.vertices.reserve(num_triangles + 2);
    mesh.vertices.push_back({0, 0, 0});
    mesh.vertices.push_back({1, 0, 0});
    for (std::size_t i = 0; i < num_triangles; ++i) {
        float y = static_cast<float>(i + 1);
        mesh.vertices.push_back({0.5f, y, 0});
    }
    mesh.triangles.reserve(num_triangles);
    for (std::size_t i = 0; i < num_triangles; ++i) {
        mesh.triangles.push_back({0, 1, static_cast<uint32_t>(i + 2)});
    }

    n3mf::DocumentBuilder builder;
    auto obj = builder.AddMeshObject("LargeMesh", std::move(mesh));
    builder.AddBuildItem(obj);
    return builder.Build();
}
} // namespace

TEST(Writer, WatermarkDoesNotBreakOutput) {
    auto doc = MakeLargeMeshDocument(1000);
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    opts.watermark.payload = {0x01, 0x02, 0x03, 0x04, 0x05};
    opts.watermark.key = {0xAA, 0xBB, 0xCC};

    auto buf = n3mf::WriteToBuffer(doc, opts);
    EXPECT_TRUE(HasZipMagic(buf));
    EXPECT_TRUE(ContainsSubstring(buf, "<vertex"));
    EXPECT_TRUE(ContainsSubstring(buf, "<triangle"));
}

TEST(Writer, WatermarkDisabledByDefault) {
    auto doc = MakeSingleTriangleDocument();
    n3mf::WriteOptions opts;
    opts.deterministic = true;
    opts.compression = n3mf::WriteOptions::Compression::Store;

    auto buf1 = n3mf::WriteToBuffer(doc, opts);
    auto buf2 = n3mf::WriteToBuffer(doc, opts);
    EXPECT_EQ(buf1, buf2);
}

TEST(Writer, L2ExtraFieldInModelEntry) {
    auto doc = MakeSingleTriangleDocument();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    auto buf = n3mf::WriteToBuffer(doc, opts);

    // L2 signature bytes: 33 4E 08 00 6E 65 72 6F 75 65 64 00
    std::vector<uint8_t> sig = {0x33, 0x4E, 0x08, 0x00, 0x6E, 0x65,
                                0x72, 0x6F, 0x75, 0x65, 0x64, 0x00};
    auto it = std::search(buf.begin(), buf.end(), sig.begin(), sig.end());
    EXPECT_NE(it, buf.end());
}

TEST(Writer, WatermarkWithTriangleProperties) {
    n3mf::Mesh mesh;
    mesh.vertices.reserve(102);
    mesh.vertices.push_back({0, 0, 0});
    mesh.vertices.push_back({1, 0, 0});
    for (int i = 0; i < 100; ++i) { mesh.vertices.push_back({0.5f, static_cast<float>(i + 1), 0}); }
    for (int i = 0; i < 100; ++i) {
        mesh.triangles.push_back({0, 1, static_cast<uint32_t>(i + 2)});
        mesh.triangle_properties.push_back({1, 0, static_cast<uint32_t>(i % 2), 0});
    }

    n3mf::DocumentBuilder builder;
    builder.AddBaseMaterialGroup({{"Red", {255, 0, 0, 255}}, {"Blue", {0, 0, 255, 255}}});
    auto obj = builder.AddMeshObject("Colored", std::move(mesh));
    builder.AddBuildItem(obj);

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    opts.watermark.payload = {0x42};
    opts.watermark.key = {0x01};

    auto buf = n3mf::WriteToBuffer(doc, opts);
    EXPECT_TRUE(HasZipMagic(buf));
    EXPECT_TRUE(ContainsSubstring(buf, "pid=\"1\""));
}

// -- CanonicalRotation unit tests --

TEST(CanonicalRotation, Distinct) {
    using n3mf::detail::CanonicalRotation;
    // (5,3,7): canonical = (3,7,5) at rotation 1
    EXPECT_EQ(CanonicalRotation(5, 3, 7), 1);
    EXPECT_EQ(CanonicalRotation(3, 7, 5), 0);
    EXPECT_EQ(CanonicalRotation(7, 5, 3), 2);
}

TEST(CanonicalRotation, TwoEqual) {
    using n3mf::detail::CanonicalRotation;
    // (3,3,7): canonical = (3,3,7) at rotation 0
    EXPECT_EQ(CanonicalRotation(3, 3, 7), 0);
    EXPECT_EQ(CanonicalRotation(3, 7, 3), 2);
    EXPECT_EQ(CanonicalRotation(7, 3, 3), 1);
}

TEST(CanonicalRotation, AllEqual) {
    using n3mf::detail::CanonicalRotation;
    EXPECT_EQ(CanonicalRotation(5, 5, 5), 0);
}

// -- ScanTrianglesFromXml unit tests --

TEST(ScanTriangles, BasicXml) {
    std::string_view xml = R"(<?xml version="1.0"?>
<model><resources><object id="1"><mesh>
<vertices><vertex x="0" y="0" z="0"/></vertices>
<triangles>
  <triangle v1="0" v2="1" v3="2"/>
  <triangle v1="3" v2="4" v3="5" pid="1" p1="0" p2="0" p3="0"/>
</triangles>
</mesh></object></resources></model>)";
    auto tris = n3mf::detail::ScanTrianglesFromXml(xml);
    ASSERT_EQ(tris.size(), 2u);
    EXPECT_EQ(tris[0][0], 0u);
    EXPECT_EQ(tris[0][1], 1u);
    EXPECT_EQ(tris[0][2], 2u);
    EXPECT_EQ(tris[1][0], 3u);
    EXPECT_EQ(tris[1][1], 4u);
    EXPECT_EQ(tris[1][2], 5u);
}

TEST(ScanTriangles, SkipsTrianglesTag) {
    std::string_view xml = "<triangles><triangle v1=\"0\" v2=\"1\" v3=\"2\"/></triangles>";
    auto tris = n3mf::detail::ScanTrianglesFromXml(xml);
    ASSERT_EQ(tris.size(), 1u);
}

// -- Watermark detection end-to-end tests --

TEST(WatermarkDetect, RoundTripFlatModel) {
    auto doc = MakeLargeMeshDocument(2000);
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    opts.watermark.payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
    opts.watermark.key = {0xAA, 0xBB, 0xCC, 0xDD};
    opts.watermark.repetition = 3;

    auto buf = n3mf::WriteToBuffer(doc, opts);

    auto result = n3mf::DetectWatermark(buf, opts.watermark.key);
    EXPECT_TRUE(result.has_l2_signature);
    EXPECT_TRUE(result.has_l1_payload);
    EXPECT_FALSE(result.payload_truncated);
    EXPECT_EQ(result.payload, opts.watermark.payload);
}

TEST(WatermarkDetect, RoundTripDeflate) {
    auto doc = MakeLargeMeshDocument(2000);
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Deflate;
    opts.watermark.payload = {0xDE, 0xAD};
    opts.watermark.key = {0x01, 0x02};
    opts.watermark.repetition = 1;

    auto buf = n3mf::WriteToBuffer(doc, opts);

    auto result = n3mf::DetectWatermark(buf, opts.watermark.key);
    EXPECT_TRUE(result.has_l2_signature);
    EXPECT_TRUE(result.has_l1_payload);
    EXPECT_EQ(result.payload, opts.watermark.payload);
}

TEST(WatermarkDetect, RoundTripNoKey) {
    auto doc = MakeLargeMeshDocument(2000);
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    opts.watermark.payload = {0x42, 0x43, 0x44};
    opts.watermark.key = {};
    opts.watermark.repetition = 3;

    auto buf = n3mf::WriteToBuffer(doc, opts);

    auto result = n3mf::DetectWatermark(buf, {});
    EXPECT_TRUE(result.has_l1_payload);
    EXPECT_EQ(result.payload, opts.watermark.payload);
}

TEST(WatermarkDetect, L2OnlyNoWatermark) {
    auto doc = MakeSingleTriangleDocument();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;

    auto buf = n3mf::WriteToBuffer(doc, opts);

    EXPECT_TRUE(n3mf::HasL2Signature(buf));

    auto result = n3mf::DetectWatermark(buf, {0x01});
    EXPECT_TRUE(result.has_l2_signature);
    EXPECT_FALSE(result.has_l1_payload);
    EXPECT_TRUE(result.payload.empty());
}

TEST(WatermarkDetect, WrongKeyFailsDecode) {
    auto doc = MakeLargeMeshDocument(2000);
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    opts.watermark.payload = {0x01, 0x02, 0x03};
    opts.watermark.key = {0xAA, 0xBB};

    auto buf = n3mf::WriteToBuffer(doc, opts);

    auto result = n3mf::DetectWatermark(buf, {0xFF, 0xEE});
    EXPECT_TRUE(result.has_l2_signature);
    EXPECT_FALSE(result.has_l1_payload);
}

TEST(WatermarkDetect, ProductionModePerObject) {
    n3mf::DocumentBuilder builder;
    builder.EnableProduction();

    for (int i = 0; i < 3; ++i) {
        n3mf::Mesh mesh;
        float offset = static_cast<float>(i * 20);
        mesh.vertices.reserve(502);
        mesh.vertices.push_back({offset, 0, 0});
        mesh.vertices.push_back({offset + 1, 0, 0});
        for (int j = 0; j < 500; ++j) {
            mesh.vertices.push_back({offset + 0.5f, static_cast<float>(j + 1), 0});
        }
        mesh.triangles.reserve(500);
        for (int j = 0; j < 500; ++j) {
            mesh.triangles.push_back({0, 1, static_cast<uint32_t>(j + 2)});
        }
        auto obj = builder.AddMeshObject("Part_" + std::to_string(i), std::move(mesh));
        builder.AddBuildItem(obj);
    }

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    opts.watermark.payload = {0xCA, 0xFE};
    opts.watermark.key = {0x11, 0x22, 0x33};
    opts.watermark.repetition = 3;

    auto buf = n3mf::WriteToBuffer(doc, opts);

    auto result = n3mf::DetectWatermark(buf, opts.watermark.key);
    EXPECT_TRUE(result.has_l2_signature);
    EXPECT_TRUE(result.has_l1_payload);
    EXPECT_EQ(result.payload, opts.watermark.payload);
}

TEST(WatermarkDetect, ProductionModeMerged) {
    n3mf::DocumentBuilder builder;
    builder.EnableProduction().SetProductionMergeObjects(true);

    for (int i = 0; i < 2; ++i) {
        n3mf::Mesh mesh;
        float offset = static_cast<float>(i * 20);
        mesh.vertices.reserve(502);
        mesh.vertices.push_back({offset, 0, 0});
        mesh.vertices.push_back({offset + 1, 0, 0});
        for (int j = 0; j < 500; ++j) {
            mesh.vertices.push_back({offset + 0.5f, static_cast<float>(j + 1), 0});
        }
        mesh.triangles.reserve(500);
        for (int j = 0; j < 500; ++j) {
            mesh.triangles.push_back({0, 1, static_cast<uint32_t>(j + 2)});
        }
        auto obj = builder.AddMeshObject("Part_" + std::to_string(i), std::move(mesh));
        builder.AddBuildItem(obj);
    }

    auto doc = builder.Build();
    n3mf::WriteOptions opts;
    opts.compression = n3mf::WriteOptions::Compression::Store;
    opts.watermark.payload = {0xBE, 0xEF};
    opts.watermark.key = {0x44, 0x55};

    auto buf = n3mf::WriteToBuffer(doc, opts);

    auto result = n3mf::DetectWatermark(buf, opts.watermark.key);
    EXPECT_TRUE(result.has_l2_signature);
    EXPECT_TRUE(result.has_l1_payload);
    EXPECT_EQ(result.payload, opts.watermark.payload);
}

TEST(WatermarkDetect, HasL2SignatureOnNonLibraryData) {
    std::vector<uint8_t> fake_zip = {0x50, 0x4B, 0x03, 0x04, 0x00, 0x00};
    EXPECT_FALSE(n3mf::HasL2Signature(fake_zip));
}

TEST(WatermarkDetect, EmptyBufferReturnsFalse) {
    std::vector<uint8_t> empty;
    EXPECT_FALSE(n3mf::HasL2Signature(empty));

    auto result = n3mf::DetectWatermark(empty, {});
    EXPECT_FALSE(result.has_l2_signature);
    EXPECT_FALSE(result.has_l1_payload);
}
