# neroued_3mf

Lightweight C++20 library for writing [3MF](https://3mf.io/specification/) files.

## Highlights

- **Zero-copy streaming write** — `MeshView` references external buffers via `std::span`; hot path has zero heap allocations
- **Three steps**: `DocumentBuilder` -> `Document` -> `WriteToFile` / `WriteToBuffer` / `WriteToStream`
- **Vendor-neutral extensions** — inject arbitrary files, OPC relationships, and metadata without library changes
- **Production Extension** — assembly models, external object files, merged mode, `p:UUID`
- **No runtime dependencies** — C++20 standard library only (zlib optional for deflate compression)

## Quick Start

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

## Installation

```cmake
include(FetchContent)
FetchContent_Declare(neroued_3mf
    GIT_REPOSITORY https://github.com/neroued/neroued_3mf.git
    GIT_TAG v0.1.0)
set(N3MF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(neroued_3mf)
target_link_libraries(your_target PRIVATE neroued::3mf)
```

Other integration methods (add_subdirectory, find_package) are documented in [Building & Integration](docs/BUILDING.md).

## Documentation

| Document | Description |
|----------|-------------|
| [Building & Integration](docs/BUILDING.md) | Dependencies, CMake options, all integration methods |
| [API Reference](docs/API.md) | Complete type and method reference |
| [Usage Examples](docs/EXAMPLES.md) | MeshView, components, Production mode, vendor extensions |
| [Design & Architecture](docs/DESIGN.md) | Design decisions, internal architecture, 3MF standard coverage |

## License

This project is dual-licensed:

- **AGPL-3.0** for open-source use — see [LICENSE](LICENSE)
- **Commercial License** for proprietary use — see [COMMERCIAL_LICENSE.md](COMMERCIAL_LICENSE.md)

If you cannot comply with AGPL-3.0 (e.g. closed-source integration), a commercial license is available. Contact the author for details.
