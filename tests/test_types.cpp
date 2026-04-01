#include "neroued/3mf/types.h"

#include <gtest/gtest.h>

namespace n3mf = neroued_3mf;

TEST(Color, ToHexOpaque) {
    n3mf::Color red{255, 0, 0, 255};
    n3mf::Color blueish{0, 128, 255, 255};
    EXPECT_EQ(red.ToHex(), "#FF0000");
    EXPECT_EQ(blueish.ToHex(), "#0080FF");
}

TEST(Color, ToHexWithAlpha) {
    n3mf::Color c{255, 0, 0, 128};
    EXPECT_EQ(c.ToHex(), "#FF000080");
}

TEST(Color, FromHexRGB) {
    auto c = n3mf::Color::FromHex("#FF8000");
    EXPECT_EQ(c.r, 255);
    EXPECT_EQ(c.g, 128);
    EXPECT_EQ(c.b, 0);
    EXPECT_EQ(c.a, 255);
}

TEST(Color, FromHexRGBA) {
    auto c = n3mf::Color::FromHex("#FF800040");
    EXPECT_EQ(c.r, 255);
    EXPECT_EQ(c.g, 128);
    EXPECT_EQ(c.b, 0);
    EXPECT_EQ(c.a, 64);
}

TEST(Color, Roundtrip) {
    n3mf::Color original;
    original.r = 100;
    original.g = 200;
    original.b = 50;
    original.a = 255;
    auto hex = original.ToHex();
    auto parsed = n3mf::Color::FromHex(hex);
    EXPECT_EQ(parsed.r, original.r);
    EXPECT_EQ(parsed.g, original.g);
    EXPECT_EQ(parsed.b, original.b);
    EXPECT_EQ(parsed.a, original.a);
}

TEST(Transform, IdentityDefault) {
    auto t = n3mf::Transform::Identity();
    EXPECT_TRUE(t.IsIdentity());
}

TEST(Transform, Translation) {
    auto t = n3mf::Transform::Translation(10.0f, 20.0f, 30.0f);
    EXPECT_FALSE(t.IsIdentity());
    EXPECT_FLOAT_EQ(t.m[9], 10.0f);
    EXPECT_FLOAT_EQ(t.m[10], 20.0f);
    EXPECT_FLOAT_EQ(t.m[11], 30.0f);
    EXPECT_FLOAT_EQ(t.m[0], 1.0f);
}

TEST(Mesh, EmptyMesh) {
    n3mf::Mesh m;
    EXPECT_TRUE(m.Empty());
    EXPECT_EQ(m.VertexCount(), 0u);
    EXPECT_EQ(m.TriangleCount(), 0u);
    EXPECT_FALSE(m.HasTriangleProperties());
}

TEST(Mesh, BasicTriangle) {
    n3mf::Mesh m;
    m.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    m.triangles = {{0, 1, 2}};

    EXPECT_FALSE(m.Empty());
    EXPECT_EQ(m.VertexCount(), 3u);
    EXPECT_EQ(m.TriangleCount(), 1u);

    auto bbox = m.ComputeBoundingBox();
    EXPECT_FLOAT_EQ(bbox.min.x, 0.0f);
    EXPECT_FLOAT_EQ(bbox.max.x, 1.0f);
    EXPECT_FLOAT_EQ(bbox.max.y, 1.0f);
}

TEST(Mesh, Validate) {
    n3mf::Mesh m;
    m.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    m.triangles = {{0, 1, 2}};
    auto result = m.Validate();
    EXPECT_TRUE(result.Valid());
    EXPECT_EQ(result.degenerate_count, 0u);
    EXPECT_EQ(result.out_of_range_count, 0u);
}

TEST(Mesh, ValidateOutOfRange) {
    n3mf::Mesh m;
    m.vertices = {{0, 0, 0}, {1, 0, 0}};
    m.triangles = {{0, 1, 5}};
    auto result = m.Validate();
    EXPECT_FALSE(result.Valid());
    EXPECT_EQ(result.out_of_range_count, 1u);
}

TEST(Mesh, ValidateDegenerate) {
    n3mf::Mesh m;
    m.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    m.triangles = {{0, 0, 1}, {0, 1, 2}};
    auto result = m.Validate();
    EXPECT_TRUE(result.Valid());
    EXPECT_EQ(result.degenerate_count, 1u);
}

TEST(Mesh, RemoveDegenerateTriangles) {
    n3mf::Mesh m;
    m.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    m.triangles = {{0, 0, 1}, {0, 1, 2}, {0, 0, 0}};
    auto removed = m.RemoveDegenerateTriangles();
    EXPECT_EQ(removed, 2u);
    EXPECT_EQ(m.TriangleCount(), 1u);
    EXPECT_EQ(m.triangles[0].v1, 0u);
    EXPECT_EQ(m.triangles[0].v2, 1u);
    EXPECT_EQ(m.triangles[0].v3, 2u);
}

TEST(Mesh, Append) {
    n3mf::Mesh a;
    a.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    a.triangles = {{0, 1, 2}};

    n3mf::Mesh b;
    b.vertices = {{2, 0, 0}, {3, 0, 0}, {2, 1, 0}};
    b.triangles = {{0, 1, 2}};

    a.Append(b.View());
    EXPECT_EQ(a.VertexCount(), 6u);
    EXPECT_EQ(a.TriangleCount(), 2u);
    EXPECT_EQ(a.triangles[1].v1, 3u);
    EXPECT_EQ(a.triangles[1].v2, 4u);
    EXPECT_EQ(a.triangles[1].v3, 5u);
}

TEST(Mesh, ImplicitConversionToMeshView) {
    n3mf::Mesh m;
    m.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    m.triangles = {{0, 1, 2}};

    n3mf::MeshView view = m;
    EXPECT_EQ(view.vertices.size(), 3u);
    EXPECT_EQ(view.triangles.size(), 1u);
    EXPECT_TRUE(view.triangle_properties.empty());
}

TEST(MeshView, ExternalBufferReference) {
    std::vector<n3mf::Vec3f> verts = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    std::vector<n3mf::IndexTriangle> tris = {{0, 1, 2}};

    n3mf::MeshView view{verts, tris, {}};
    EXPECT_EQ(view.vertices.size(), 3u);
    EXPECT_EQ(view.triangles.size(), 1u);
    EXPECT_FLOAT_EQ(view.vertices[1].x, 1.0f);
}

TEST(AsVertexSpan, FromRawFloatPointer) {
    float data[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    auto span = n3mf::AsVertexSpan(data, 2);
    EXPECT_EQ(span.size(), 2u);
    EXPECT_FLOAT_EQ(span[0].x, 1.0f);
    EXPECT_FLOAT_EQ(span[0].y, 2.0f);
    EXPECT_FLOAT_EQ(span[0].z, 3.0f);
    EXPECT_FLOAT_EQ(span[1].x, 4.0f);
}

TEST(AsVertexSpan, FromLayoutCompatibleStruct) {
    struct MyVec3 {
        float x, y, z;
    };
    MyVec3 data[] = {{10.0f, 20.0f, 30.0f}, {40.0f, 50.0f, 60.0f}};
    auto span = n3mf::AsVertexSpan(data, 2);
    EXPECT_EQ(span.size(), 2u);
    EXPECT_FLOAT_EQ(span[0].x, 10.0f);
    EXPECT_FLOAT_EQ(span[1].z, 60.0f);
}

TEST(AsVertexSpan, EmptySpan) {
    auto span = n3mf::AsVertexSpan(static_cast<const float *>(nullptr), 0);
    EXPECT_EQ(span.size(), 0u);
    EXPECT_TRUE(span.empty());
}

TEST(AsTriangleSpan, FromRawUint32Pointer) {
    uint32_t data[] = {0, 1, 2, 3, 4, 5};
    auto span = n3mf::AsTriangleSpan(data, 2);
    EXPECT_EQ(span.size(), 2u);
    EXPECT_EQ(span[0].v1, 0u);
    EXPECT_EQ(span[0].v2, 1u);
    EXPECT_EQ(span[0].v3, 2u);
    EXPECT_EQ(span[1].v1, 3u);
}

TEST(AsTriangleSpan, FromLayoutCompatibleStruct) {
    struct MyTri {
        uint32_t a, b, c;
    };
    MyTri data[] = {{10, 20, 30}};
    auto span = n3mf::AsTriangleSpan(data, 1);
    EXPECT_EQ(span.size(), 1u);
    EXPECT_EQ(span[0].v1, 10u);
    EXPECT_EQ(span[0].v2, 20u);
    EXPECT_EQ(span[0].v3, 30u);
}

TEST(AsVertexSpan, UsableInMeshView) {
    float verts[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    uint32_t tris[] = {0, 1, 2};
    n3mf::MeshView view{
        n3mf::AsVertexSpan(verts, 3),
        n3mf::AsTriangleSpan(tris, 1),
        {},
    };
    EXPECT_EQ(view.vertices.size(), 3u);
    EXPECT_EQ(view.triangles.size(), 1u);
    EXPECT_FLOAT_EQ(view.vertices[2].y, 1.0f);
}
