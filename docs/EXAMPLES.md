# Usage Examples

## Basic: Single Mesh with Materials

```cpp
#include <neroued/3mf/neroued_3mf.h>

neroued_3mf::Mesh mesh;
mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 5}};
mesh.triangles = {{0, 1, 2}};

neroued_3mf::DocumentBuilder builder;
builder.SetUnit(neroued_3mf::Unit::Millimeter);
builder.AddMetadata("Application", "MyApp");

auto mat = builder.AddBaseMaterialGroup({
    {"Red", {255, 0, 0, 255}},
    {"Blue", {0, 0, 255, 255}},
});

auto obj = builder.AddMeshObject("Part1", std::move(mesh), mat, 0);
builder.AddBuildItem(obj);

auto doc = builder.Build();
neroued_3mf::WriteToFile("output.3mf", doc);
```

## Zero-copy with MeshView

When your mesh data already lives in external buffers, use `MeshView` to avoid copying. The helper functions `AsVertexSpan` / `AsTriangleSpan` convert layout-compatible data (raw `float*` / `uint32_t*` or custom structs) into the spans that `MeshView` expects:

```cpp
// Scenario 1: Raw float*/uint32_t* from a C API or flat buffer
float *raw_verts = /* ... */;
uint32_t *raw_idx = /* ... */;
neroued_3mf::MeshView view{
    neroued_3mf::AsVertexSpan(raw_verts, vert_count),
    neroued_3mf::AsTriangleSpan(raw_idx, tri_count),
    {}
};

// Scenario 2: Layout-compatible custom struct array
struct MyVec3 { float x, y, z; };
std::vector<MyVec3> my_verts = /* ... */;
neroued_3mf::MeshView view2{
    neroued_3mf::AsVertexSpan(my_verts.data(), my_verts.size()),
    neroued_3mf::AsTriangleSpan(raw_idx, tri_count),
    {}
};

// Scenario 3: std::vector<Vec3f> / std::vector<IndexTriangle> — implicit span
std::vector<neroued_3mf::Vec3f> vertices = /* ... */;
std::vector<neroued_3mf::IndexTriangle> indices = /* ... */;
neroued_3mf::MeshView view3{vertices, indices, {}};

auto obj = builder.AddMeshObject("External", view);
builder.AddBuildItem(obj);
auto doc = builder.Build();
auto buffer = neroued_3mf::WriteToBuffer(doc);
// Caller must keep vertex/index data alive until Write* returns.
```

## Assembly with Core Components

Objects can reference other objects through `<components>` to build assemblies within a single model file:

```cpp
neroued_3mf::Mesh leaf_mesh;
leaf_mesh.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
leaf_mesh.triangles = {{0, 1, 2}};

neroued_3mf::DocumentBuilder builder;
auto leaf = builder.AddMeshObject("Leaf", std::move(leaf_mesh));
auto assembly = builder.AddComponentObject("Assembly", {
    {leaf, neroued_3mf::Transform::Translation(10, 0, 0)},
    {leaf, neroued_3mf::Transform::Translation(-10, 0, 0)},
});
builder.AddBuildItem(assembly);

auto doc = builder.Build();
neroued_3mf::WriteToFile("assembly.3mf", doc);
```

## Production Extension

Production mode writes each object to a separate external `.model` file, with an assembly model referencing them via `<component p:path="...">`. This is the format used by Bambu Studio and other production slicers.

```cpp
neroued_3mf::DocumentBuilder builder;
builder.EnableProduction(neroued_3mf::Transform::Translation(128, 128, 0));

neroued_3mf::Mesh m1, m2;
m1.vertices = {{0, 0, 0}, {10, 0, 0}, {5, 10, 0}};
m1.triangles = {{0, 1, 2}};
m2 = m1;

auto o1 = builder.AddMeshObject("Part1", std::move(m1));
auto o2 = builder.AddMeshObject("Part2", std::move(m2));
builder.SetObjectUUID(o1, "550e8400-e29b-41d4-a716-446655440000");
builder.SetObjectUUID(o2, "550e8400-e29b-41d4-a716-446655440001");
builder.AddBuildItem(o1);
builder.AddBuildItem(o2);

auto doc = builder.Build();
neroued_3mf::WriteToFile("production.3mf", doc);
```

### Merged Objects Mode

All objects in a single external file (Bambu Studio compatible layout):

```cpp
builder.EnableProduction(neroued_3mf::Transform::Translation(128, 128, 0))
    .SetProductionMergeObjects(true);
```

## Custom Vendor Extensions (e.g. Bambu Studio)

The library is vendor-neutral. Inject proprietary metadata through the generic extension API:

```cpp
neroued_3mf::DocumentBuilder builder;
builder.EnableProduction(center_transform)
    .SetProductionMergeObjects(true);

// Register vendor XML namespace
builder.AddNamespace("BambuStudio", "http://schemas.bambulab.com/package/2021");

// Vendor metadata in external model files
builder.AddExternalModelMetadata("BambuStudio:3mfVersion", "1");

// Add mesh objects ...
auto obj = builder.AddMeshObject("Part", std::move(mesh));
builder.AddBuildItem(obj);

// Inject vendor-specific config files
std::string settings = BuildProjectSettings(preset);
builder.AddCustomPart({
    "Metadata/project_settings.config",
    "application/octet-stream",
    {settings.begin(), settings.end()}
});

// OPC relationship so slicers can discover the config
builder.AddCustomRelationship({
    "3D/3dmodel.model",
    "rel-settings",
    "http://schemas.bambulab.com/package/2021/settings",
    "/Metadata/project_settings.config"
});

// Register .config extension content type
builder.AddCustomContentType({"config", "application/octet-stream"});

auto doc = builder.Build();
neroued_3mf::WriteToFile("bambu_project.3mf", doc);
```

## Python: NumPy Array Input

`add_mesh_object()` accepts NumPy arrays directly as an alternative to a `Mesh` object. When dtypes match the internal format (float32 + uint32/int32), data is referenced **zero-copy**; otherwise an efficient single-step conversion is performed.

```python
import numpy as np
import neroued_3mf as n3mf

builder = n3mf.DocumentBuilder()
builder.set_unit(n3mf.Unit.Millimeter)

# Approach 1: Pass NumPy arrays directly (recommended for most workflows)
# trimesh example (float64 + int64 → fast internal conversion)
import trimesh
tm = trimesh.load("model.stl")
obj = builder.add_mesh_object("Part", tm.vertices, tm.faces)
builder.add_build_item(obj)

# High-perf workflow (float32 + uint32 → true zero-copy, no data copy at all)
verts = np.array([[0, 0, 0], [10, 0, 0], [5, 10, 5]], dtype=np.float32)
tris = np.array([[0, 1, 2]], dtype=np.uint32)
obj2 = builder.add_mesh_object("Part2", verts, tris)
builder.add_build_item(obj2)

doc = builder.build()
n3mf.write_to_file("output.3mf", doc)
```

`Mesh.from_arrays()` is still available for creating standalone `Mesh` objects when you need to inspect, validate, or manipulate the mesh before adding it:

```python
mesh = n3mf.Mesh.from_arrays(verts_f64, tris_i64)
mesh.remove_degenerate_triangles()
bb = mesh.compute_bounding_box()
obj = builder.add_mesh_object("Part", mesh)
```

## Watermark: Embed and Detect

### C++: Embed watermark during write

```cpp
#include <neroued/3mf/neroued_3mf.h>

auto doc = builder.Build();

neroued_3mf::WriteOptions opts;
opts.watermark.payload = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
opts.watermark.key = {0x73, 0x65, 0x63, 0x72, 0x65, 0x74}; // "secret"
opts.watermark.repetition = 3; // 3x majority-vote redundancy
neroued_3mf::WriteToFile("watermarked.3mf", doc, opts);
```

### C++: Detect watermark from a 3MF file

```cpp
#include <neroued/3mf/watermark.h>

std::vector<uint8_t> data = /* read file into memory */;
std::vector<uint8_t> key = {0x73, 0x65, 0x63, 0x72, 0x65, 0x74};

auto result = neroued_3mf::DetectWatermark(data, key);
if (result.has_l2_signature) { /* Library fingerprint found */ }
if (result.has_l1_payload)  { /* result.payload contains decoded bytes */ }

// Fast L2-only check (no XML parsing)
bool from_lib = neroued_3mf::HasL2Signature(data);
```

### Python: Embed and detect watermark

```python
import neroued_3mf as n3mf

# Embed
opts = n3mf.WriteOptions()
opts.watermark = n3mf.WatermarkConfig(
    payload=b"Hello",
    key=b"secret",
    repetition=3,
)
buf = n3mf.write_to_buffer(doc, opts)

# Detect
result = n3mf.detect_watermark(buf, key=b"secret")
print(result.has_l2_signature)  # True
print(result.has_l1_payload)    # True
print(result.payload)           # b"Hello"

# Fast L2-only check
print(n3mf.has_l2_signature(buf))  # True
```

## Write Options

### Compression Control

```cpp
neroued_3mf::WriteOptions opts;
opts.compression = neroued_3mf::WriteOptions::Compression::Deflate;
opts.deflate_level = 6;
auto buf = neroued_3mf::WriteToBuffer(doc, opts);
```

### Vertex Precision

Reduce vertex coordinate digits to shrink XML size (default 9 = full float32 precision):

```cpp
opts.vertex_precision = 6;  // 6 significant digits (~30% smaller vertex XML)
```

Transform matrices always use full precision regardless of this setting.

### Compact XML

Reduces XML output size by ~15-20% by omitting indentation:

```cpp
opts.compact_xml = true;
```

### Deterministic Output

Enabled by default. Produces identical bytes for identical input (zero timestamps):

```cpp
opts.deterministic = true;
auto buf1 = neroued_3mf::WriteToBuffer(doc, opts);
auto buf2 = neroued_3mf::WriteToBuffer(doc, opts);
// buf1 == buf2
```

### Write to Stream

```cpp
std::ofstream ofs("output.3mf", std::ios::binary);
neroued_3mf::WriteToStream(ofs, doc);
```
