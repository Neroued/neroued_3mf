# neroued_3mf

Lightweight C++20 library for writing 3MF files, targeting the [3MF Core Specification v1.4](https://3mf.io/specification/).

## Features

- **Zero-copy mesh export** via `MeshView` (`std::span` references to external vertex/index buffers)
- **Owning mesh type** (`Mesh`) with validation, bounding box, degenerate triangle removal, and merge utilities
- **Document-first streaming write** — build a lightweight `Document`, then stream to ZIP with minimal peak memory (zero heap allocations in hot path)
- **ZIP64 support** — archives exceeding 4 GB
- **Object types** — `model`, `solidsupport`, `support`, `surface`, `other` per 3MF Core Spec
- **Core-spec components** — intra-model assembly via `<components>` referencing other objects
- **Production Extension** — external model parts with assembly components, merged objects mode, custom XML namespaces, and external model metadata
- **Per-triangle properties** (`pid`/`p1`/`p2`/`p3`) for multi-material support
- **Per-object metadata** via `<metadatagroup>` with optional type annotation
- **Package thumbnail** — embed PNG/JPEG preview images
- **Custom XML namespaces** on all model root elements
- **Generic extension injection** via `CustomPart` (binary data) / `CustomRelationship` / `CustomContentType` (vendor-agnostic)
- **Three write targets**: `WriteToBuffer`, `WriteToFile` (atomic), `WriteToStream`
- **Optional zlib compression** (deflate) with automatic fallback to store-only
- **Deterministic output** for reproducible builds
- **Document validation** — `Build()` and `Write*` validate internal consistency
- **Compile-time version macros** via `<neroued/3mf/version.h>`
- No third-party dependencies beyond the C++ standard library (zlib is optional)

## Quick Start

```cpp
#include <neroued/3mf/neroued_3mf.h>  // or include individual headers

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

### Zero-copy with MeshView

```cpp
// External buffers (e.g. from a meshing library)
std::vector<neroued_3mf::Vec3f> vertices = /* ... */;
std::vector<neroued_3mf::IndexTriangle> indices = /* ... */;

neroued_3mf::MeshView view{vertices, indices, {}};
auto obj = builder.AddMeshObject("External", view);
// Caller must keep vertices/indices alive until WriteToBuffer/WriteToFile returns.
```

### Assembly with core components

```cpp
auto leaf = builder.AddMeshObject("Leaf", std::move(mesh));
auto assembly = builder.AddComponentObject("Assembly", {
    {leaf, neroued_3mf::Transform::Translation(10, 0, 0)},
    {leaf, neroued_3mf::Transform::Translation(-10, 0, 0)},
});
builder.AddBuildItem(assembly);
```

### Custom vendor extensions

```cpp
std::string settings = BuildProjectSettings(preset);
builder.AddCustomPart({
    "Metadata/project_settings.config",
    "application/octet-stream",
    {settings.begin(), settings.end()}
});
builder.AddCustomRelationship({
    "3D/3dmodel.model", "rel-settings",
    "http://vendor.example.com/settings",
    "/Metadata/project_settings.config"
});
builder.AddCustomContentType({"config", "application/octet-stream"});
```

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `N3MF_BUILD_TESTS` | `ON` | Build unit tests (requires internet for GoogleTest fetch) |

### Dependencies

- **C++20** compiler (GCC 11+, Clang 14+, MSVC 2022+)
- **zlib** (optional) — enables deflate compression; without it, store-only is used
- **GoogleTest** (fetched automatically when tests are enabled)

### Integration

**add_subdirectory:**

```cmake
add_subdirectory(path/to/neroued_3mf)
target_link_libraries(your_target PRIVATE neroued::3mf)
```

**FetchContent:**

```cmake
include(FetchContent)
FetchContent_Declare(neroued_3mf
    GIT_REPOSITORY https://github.com/neroued/neroued_3mf.git
    GIT_TAG v0.1.0)
set(N3MF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(neroued_3mf)
target_link_libraries(your_target PRIVATE neroued::3mf)
```

**find_package (after install):**

```cmake
find_package(neroued_3mf 0.1 REQUIRED)
target_link_libraries(your_target PRIVATE neroued::3mf)
```

## API Overview

### Types (`types.h`)

| Type | Description |
|------|-------------|
| `Vec3f` | 3D float vector `{x, y, z}` |
| `IndexTriangle` | Triangle indices `{v1, v2, v3}` as `uint32_t` |
| `TriangleProperty` | Per-triangle material property `{pid, p1, p2, p3}` |
| `MeshView` | Non-owning spans over external buffers |
| `Mesh` | Owning mesh with utility methods |
| `Color` | RGBA color with hex conversion |
| `Transform` | 3MF affine transform (3x4 matrix) |

### Materials (`materials.h`)

| Type | Description |
|------|-------------|
| `BaseMaterial` | Named material with display color |
| `BaseMaterialGroup` | Group of base materials (3MF `<basematerials>`) |

### Document (`document.h`)

| Type | Description |
|------|-------------|
| `Document` | Complete 3MF document model |
| `ObjectType` | Enum: `Model`, `SolidSupport`, `Support`, `Surface`, `Other` |
| `Object` | Mesh or component object with type, partnumber, metadata |
| `Component` | Intra-model object reference with transform |
| `BuildItem` | Build plate item with transform and partnumber |
| `Metadata` | Name-value pair with optional type annotation |
| `XmlNamespace` | XML namespace declaration (prefix + URI) for model roots |
| `Thumbnail` | Binary image data (PNG/JPEG) for package thumbnail |
| `CustomPart` | Arbitrary ZIP entry (binary data) for vendor extensions |
| `CustomRelationship` | OPC relationship for vendor extensions |
| `CustomContentType` | File extension to MIME type mapping |

### Builder (`builder.h`)

`DocumentBuilder` provides a type-safe fluent API for constructing a `Document` with automatic resource ID assignment. Key methods:

| Method | Description |
|--------|-------------|
| `SetUnit`, `SetLanguage` | Document-level settings |
| `AddMetadata(name, value, type)` | Model-level metadata |
| `AddNamespace(prefix, uri)` | Custom XML namespace |
| `AddBaseMaterialGroup(materials)` | Add material group, returns ID |
| `AddMeshObject(name, mesh, ...)` | Add mesh object, returns ID |
| `AddComponentObject(name, components)` | Add assembly object, returns ID |
| `SetObjectType`, `SetPartNumber` | Per-object attributes |
| `SetObjectUUID(id, uuid)` | Set Production Extension p:UUID on object |
| `AddObjectMetadata`, `SetComponentTransform` | Per-object metadata and production transform |
| `AddBuildItem(id, transform, partnumber, uuid)` | Add build plate item (uuid for Production Extension) |
| `SetThumbnail(data, content_type)` | Set package thumbnail |
| `EnableProduction`, `SetProductionMergeObjects` | Production Extension config |
| `AddCustomPart`, `AddCustomRelationship` | Vendor extension injection |
| `Build()` | Validate and produce `Document` |

### Writer (`writer.h`)

| Function | Description |
|----------|-------------|
| `WriteToBuffer(doc, opts)` | Serialize to `std::vector<uint8_t>` |
| `WriteToFile(path, doc, opts)` | Atomic write to file |
| `WriteToStream(out, doc, opts)` | Write to `std::ostream` |

`WriteOptions` fields: `compression` (Store/Deflate/Auto), `compression_threshold`, `deflate_level`, `deterministic`, `compact_xml`.

## Architecture

```
Caller (e.g. ChromaPrint3D)
    │
    ├── DocumentBuilder  ──▶  Document (lightweight, MeshView references)
    │                              │
    │                              ▼
    │                    WriteToBuffer / WriteToFile / WriteToStream
    │                              │
    │                     ┌────────┴────────┐
    │                     ▼                 ▼
    │              StreamMeshXml      OPC metadata
    │              (per-vertex/tri    (rels, content
    │               batched XML)      types, XML)
    │                     │                 │
    │                     └────────┬────────┘
    │                              ▼
    │                    StreamingZipWriter
    │                    (deflate / store)
    │                              │
    │                              ▼
    │                      .3mf ZIP package
```

## License

This project is dual-licensed:

- **AGPL-3.0** for open-source use — see [LICENSE](LICENSE)
- **Commercial License** for proprietary use — see [COMMERCIAL_LICENSE.md](COMMERCIAL_LICENSE.md)

If you cannot comply with AGPL-3.0 (e.g. closed-source integration), a commercial license is available. Contact the author for details.
