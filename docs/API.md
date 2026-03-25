# API Reference

All public types reside in the `neroued_3mf` namespace. Include `<neroued/3mf/neroued_3mf.h>` for everything, or individual headers listed below.

## Core Types (`types.h`)

| Type | Description |
|------|-------------|
| `Vec3f` | 3D float vector `{x, y, z}` — binary-compatible with common 3D library vector types |
| `IndexTriangle` | Triangle indices `{v1, v2, v3}` as `uint32_t` |
| `TriangleProperty` | Per-triangle material property `{pid, p1, p2, p3}` |
| `MeshView` | Non-owning spans (`std::span`) over external vertex/index/property buffers |
| `Mesh` | Owning mesh with `Validate()`, `RemoveDegenerateTriangles()`, `Append()`, `ComputeBoundingBox()` |
| `Color` | RGBA color with `ToHex()` / `FromHex()` conversion |
| `Transform` | 3MF affine transform (3x4 matrix) with `Identity()`, `Translation()`, `IsIdentity()` |

`Mesh` implicitly converts to `MeshView`, so all APIs accepting `MeshView` also accept `Mesh`.

## Materials (`materials.h`)

| Type | Description |
|------|-------------|
| `BaseMaterial` | Named material with display color (`{name, display_color}`) |
| `BaseMaterialGroup` | Group of base materials, maps to 3MF `<basematerials>` |

## Document Model (`document.h`)

| Type | Description |
|------|-------------|
| `Document` | Complete 3MF document model (unit, language, objects, build items, metadata, extensions) |
| `ObjectType` | Enum: `Model`, `SolidSupport`, `Support`, `Surface`, `Other` |
| `Object` | Mesh or component object with type, partnumber, per-object metadata, uuid |
| `Component` | Intra-model object reference with transform |
| `BuildItem` | Build plate item with transform, partnumber, uuid |
| `Metadata` | Name-value pair with optional type annotation |
| `XmlNamespace` | XML namespace declaration (prefix + URI) for model root elements |
| `Thumbnail` | Binary image data (PNG/JPEG) for package thumbnail |
| `CustomPart` | Arbitrary ZIP entry (path + content type + binary data) for vendor extensions |
| `CustomRelationship` | OPC relationship for vendor extensions |
| `CustomContentType` | File extension to MIME type mapping |
| `ProductionConfig` | Production Extension settings (enabled, merge, assembly transform, external metadata) |

## Builder (`builder.h`)

`DocumentBuilder` provides a type-safe fluent API for constructing a `Document` with automatic resource ID assignment.

### Document-level

| Method | Description |
|--------|-------------|
| `SetUnit(unit)` | Set document unit (default: Millimeter) |
| `SetLanguage(lang)` | Set `xml:lang` (default: "en-US") |
| `AddMetadata(name, value, type)` | Add model-level metadata |
| `AddNamespace(prefix, uri)` | Register custom XML namespace (deduplicates by prefix) |

### Resources

| Method | Description |
|--------|-------------|
| `AddBaseMaterialGroup(materials)` | Add material group, returns auto-assigned ID |
| `AddMeshObject(name, MeshView, pid, pindex)` | Add mesh object (zero-copy), returns ID |
| `AddMeshObject(name, Mesh&&, pid, pindex)` | Add mesh object (takes ownership), returns ID |
| `AddComponentObject(name, components)` | Add assembly object, returns ID |

### Per-object

| Method | Description |
|--------|-------------|
| `SetObjectType(id, type)` | Set object type |
| `SetPartNumber(id, partnumber)` | Set object part number |
| `SetObjectUUID(id, uuid)` | Set Production Extension `p:UUID` |
| `AddObjectMetadata(id, name, value, type)` | Add per-object metadata |
| `SetComponentTransform(id, transform)` | Set transform for Production component reference |

### Build & Output

| Method | Description |
|--------|-------------|
| `AddBuildItem(id, transform, partnumber, uuid)` | Add build plate item |
| `SetThumbnail(data, content_type)` | Set package thumbnail (PNG or JPEG) |
| `Build()` | Validate, consume builder state, return `Document` |

### Production Extension

| Method | Description |
|--------|-------------|
| `EnableProduction(assembly_transform)` | Enable Production mode (auto-registers `p` namespace) |
| `SetProductionMergeObjects(merge)` | All objects in single external file (Bambu Studio compatible) |
| `AddExternalModelMetadata(name, value, type)` | Metadata for external `.model` files |

### Vendor Extensions

| Method | Description |
|--------|-------------|
| `AddCustomPart(part)` | Inject arbitrary file into ZIP package |
| `AddCustomRelationship(rel)` | Add OPC relationship |
| `AddCustomContentType(ct)` | Register file extension content type |

**Lifecycle**: After `Build()` is called, the builder is consumed. Any subsequent method call throws `InputError`.

## Writer (`writer.h`)

| Function | Description |
|----------|-------------|
| `WriteToBuffer(doc, opts)` | Serialize to `std::vector<uint8_t>` |
| `WriteToFile(path, doc, opts)` | Atomic write to file (temp file + rename). `path` is `std::filesystem::path` |
| `WriteToStream(out, doc, opts)` | Write to `std::ostream` |

### WriteOptions

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `compression` | `Compression` | `Auto` | `Store`, `Deflate`, or `Auto` |
| `compression_threshold` | `size_t` | `16384` | Auto mode: deflate entries larger than this |
| `deflate_level` | `int` | `1` | zlib compression level (1-9) |
| `deterministic` | `bool` | `true` | Zero timestamps for reproducible output |
| `compact_xml` | `bool` | `false` | Omit XML indentation (~15-20% smaller) |

## Error Types (`error.h`)

| Type | Description |
|------|-------------|
| `InputError` | Invalid input data (bad mesh, missing objects, post-Build usage, etc.) |
| `IOError` | File system errors during write |
| `FormatError` | Reserved for future Reader — malformed 3MF/XML/ZIP |
