// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#include <cstring>
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>
#include <neroued/3mf/neroued_3mf.h>

namespace nb = nanobind;
using namespace nb::literals;
namespace n3mf = neroued_3mf;

static_assert(sizeof(n3mf::Vec3f) == 3 * sizeof(float));
static_assert(sizeof(n3mf::IndexTriangle) == 3 * sizeof(uint32_t));

NB_MODULE(_neroued_3mf, m) {
    m.doc() = "Python bindings for neroued_3mf — lightweight 3MF file writer";

    // ---- Exceptions --------------------------------------------------------

    nb::exception<n3mf::InputError>(m, "InputError", PyExc_ValueError);
    nb::exception<n3mf::IOError>(m, "IOError", PyExc_OSError);

    // ---- Enums -------------------------------------------------------------

    nb::enum_<n3mf::Unit>(m, "Unit")
        .value("Micron", n3mf::Unit::Micron)
        .value("Millimeter", n3mf::Unit::Millimeter)
        .value("Centimeter", n3mf::Unit::Centimeter)
        .value("Inch", n3mf::Unit::Inch)
        .value("Foot", n3mf::Unit::Foot)
        .value("Meter", n3mf::Unit::Meter);

    nb::enum_<n3mf::ObjectType>(m, "ObjectType")
        .value("Model", n3mf::ObjectType::Model)
        .value("SolidSupport", n3mf::ObjectType::SolidSupport)
        .value("Support", n3mf::ObjectType::Support)
        .value("Surface", n3mf::ObjectType::Surface)
        .value("Other", n3mf::ObjectType::Other);

    // ---- Vec3f -------------------------------------------------------------

    nb::class_<n3mf::Vec3f>(m, "Vec3f")
        .def(
            "__init__",
            [](n3mf::Vec3f *self, float x, float y, float z) { new (self) n3mf::Vec3f{x, y, z}; },
            "x"_a = 0.0f, "y"_a = 0.0f, "z"_a = 0.0f)
        .def_rw("x", &n3mf::Vec3f::x)
        .def_rw("y", &n3mf::Vec3f::y)
        .def_rw("z", &n3mf::Vec3f::z)
        .def("is_finite", &n3mf::Vec3f::IsFinite)
        .def("__eq__", &n3mf::Vec3f::operator==)
        .def("__repr__", [](const n3mf::Vec3f &v) {
            return "Vec3f(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " +
                   std::to_string(v.z) + ")";
        });

    // ---- IndexTriangle -----------------------------------------------------

    nb::class_<n3mf::IndexTriangle>(m, "IndexTriangle")
        .def(
            "__init__",
            [](n3mf::IndexTriangle *self, uint32_t v1, uint32_t v2, uint32_t v3) {
                new (self) n3mf::IndexTriangle{v1, v2, v3};
            },
            "v1"_a = 0u, "v2"_a = 0u, "v3"_a = 0u)
        .def_rw("v1", &n3mf::IndexTriangle::v1)
        .def_rw("v2", &n3mf::IndexTriangle::v2)
        .def_rw("v3", &n3mf::IndexTriangle::v3)
        .def("__eq__", &n3mf::IndexTriangle::operator==)
        .def("__repr__", [](const n3mf::IndexTriangle &t) {
            return "IndexTriangle(" + std::to_string(t.v1) + ", " + std::to_string(t.v2) + ", " +
                   std::to_string(t.v3) + ")";
        });

    // ---- TriangleProperty --------------------------------------------------

    nb::class_<n3mf::TriangleProperty>(m, "TriangleProperty")
        .def(
            "__init__",
            [](n3mf::TriangleProperty *self, uint32_t pid, uint32_t p1, uint32_t p2, uint32_t p3) {
                new (self) n3mf::TriangleProperty{pid, p1, p2, p3};
            },
            "pid"_a = 0u, "p1"_a = 0u, "p2"_a = 0u, "p3"_a = 0u)
        .def_rw("pid", &n3mf::TriangleProperty::pid)
        .def_rw("p1", &n3mf::TriangleProperty::p1)
        .def_rw("p2", &n3mf::TriangleProperty::p2)
        .def_rw("p3", &n3mf::TriangleProperty::p3)
        .def("__eq__", &n3mf::TriangleProperty::operator==)
        .def("__repr__", [](const n3mf::TriangleProperty &tp) {
            return "TriangleProperty(pid=" + std::to_string(tp.pid) +
                   ", p1=" + std::to_string(tp.p1) + ", p2=" + std::to_string(tp.p2) +
                   ", p3=" + std::to_string(tp.p3) + ")";
        });

    // ---- Color -------------------------------------------------------------

    nb::class_<n3mf::Color>(m, "Color")
        .def(
            "__init__",
            [](n3mf::Color *self, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
                new (self) n3mf::Color{r, g, b, a};
            },
            "r"_a = uint8_t{0}, "g"_a = uint8_t{0}, "b"_a = uint8_t{0}, "a"_a = uint8_t{255})
        .def_rw("r", &n3mf::Color::r)
        .def_rw("g", &n3mf::Color::g)
        .def_rw("b", &n3mf::Color::b)
        .def_rw("a", &n3mf::Color::a)
        .def("to_hex", &n3mf::Color::ToHex)
        .def_static("from_hex", &n3mf::Color::FromHex, "hex"_a)
        .def("__eq__", &n3mf::Color::operator==)
        .def("__repr__", [](const n3mf::Color &c) {
            return "Color(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " +
                   std::to_string(c.b) + ", " + std::to_string(c.a) + ")";
        });

    // ---- Transform ---------------------------------------------------------

    nb::class_<n3mf::Transform>(m, "Transform")
        .def(nb::init<>())
        .def_rw("m", &n3mf::Transform::m)
        .def_static("identity", &n3mf::Transform::Identity)
        .def_static("translation", &n3mf::Transform::Translation, "tx"_a, "ty"_a, "tz"_a)
        .def("is_identity", &n3mf::Transform::IsIdentity, "eps"_a = 1e-6f)
        .def("__eq__", &n3mf::Transform::operator==)
        .def("__repr__", [](const n3mf::Transform &t) {
            if (t.IsIdentity()) return std::string("Transform.identity()");
            return std::string("Transform(...)");
        });

    // ---- BBox --------------------------------------------------------------

    nb::class_<n3mf::BBox>(m, "BBox")
        .def(nb::init<>())
        .def_rw("min", &n3mf::BBox::min)
        .def_rw("max", &n3mf::BBox::max)
        .def("__repr__", [](const n3mf::BBox &b) {
            return "BBox(min=(" + std::to_string(b.min.x) + ", " + std::to_string(b.min.y) + ", " +
                   std::to_string(b.min.z) + "), max=(" + std::to_string(b.max.x) + ", " +
                   std::to_string(b.max.y) + ", " + std::to_string(b.max.z) + "))";
        });

    // ---- ValidationResult --------------------------------------------------

    nb::class_<n3mf::ValidationResult>(m, "ValidationResult")
        .def(nb::init<>())
        .def_ro("degenerate_count", &n3mf::ValidationResult::degenerate_count)
        .def_ro("out_of_range_count", &n3mf::ValidationResult::out_of_range_count)
        .def("valid", &n3mf::ValidationResult::Valid)
        .def("__repr__", [](const n3mf::ValidationResult &vr) {
            return "ValidationResult(degenerate=" + std::to_string(vr.degenerate_count) +
                   ", out_of_range=" + std::to_string(vr.out_of_range_count) + ")";
        });

    // ---- Mesh --------------------------------------------------------------

    nb::class_<n3mf::Mesh>(m, "Mesh")
        .def(nb::init<>())
        .def_rw("vertices", &n3mf::Mesh::vertices)
        .def_rw("triangles", &n3mf::Mesh::triangles)
        .def_rw("triangle_properties", &n3mf::Mesh::triangle_properties)
        .def_prop_ro("empty", &n3mf::Mesh::Empty)
        .def_prop_ro("vertex_count", &n3mf::Mesh::VertexCount)
        .def_prop_ro("triangle_count", &n3mf::Mesh::TriangleCount)
        .def_prop_ro("has_triangle_properties", &n3mf::Mesh::HasTriangleProperties)
        .def("compute_bounding_box", &n3mf::Mesh::ComputeBoundingBox)
        .def("validate", &n3mf::Mesh::Validate)
        .def("remove_degenerate_triangles", &n3mf::Mesh::RemoveDegenerateTriangles)
        .def(
            "append", [](n3mf::Mesh &self, const n3mf::Mesh &other) { self.Append(other.View()); },
            "other"_a)
        .def("__repr__",
             [](const n3mf::Mesh &mesh) {
                 return "Mesh(vertices=" + std::to_string(mesh.VertexCount()) +
                        ", triangles=" + std::to_string(mesh.TriangleCount()) + ")";
             })
        .def_static(
            "from_arrays",
            [](nb::ndarray<nb::numpy> vert_in, nb::ndarray<nb::numpy> tri_in) -> n3mf::Mesh {
                if (vert_in.ndim() != 2 || vert_in.shape(1) != 3)
                    throw n3mf::InputError("vertices: expected shape (N, 3)");
                if (tri_in.ndim() != 2 || tri_in.shape(1) != 3)
                    throw n3mf::InputError("triangles: expected shape (M, 3)");

                const size_t nv = vert_in.shape(0);
                const size_t nt = tri_in.shape(0);
                n3mf::Mesh mesh;
                if (nv == 0 || nt == 0) return mesh;

                mesh.vertices.resize(nv);
                mesh.triangles.resize(nt);

                auto is_c_contig = [](const nb::ndarray<nb::numpy> &a) {
                    return a.stride(0) == 3 && a.stride(1) == 1;
                };

                if (vert_in.dtype() == nb::dtype<float>()) {
                    if (!is_c_contig(vert_in))
                        throw n3mf::InputError("vertices: array must be C-contiguous");
                    std::memcpy(mesh.vertices.data(), vert_in.data(), nv * sizeof(n3mf::Vec3f));
                } else if (vert_in.dtype() == nb::dtype<double>()) {
                    if (!is_c_contig(vert_in))
                        throw n3mf::InputError("vertices: array must be C-contiguous");
                    const auto *s = static_cast<const double *>(vert_in.data());
                    for (size_t i = 0; i < nv; ++i) {
                        mesh.vertices[i].x = static_cast<float>(s[i * 3]);
                        mesh.vertices[i].y = static_cast<float>(s[i * 3 + 1]);
                        mesh.vertices[i].z = static_cast<float>(s[i * 3 + 2]);
                    }
                } else {
                    throw n3mf::InputError("vertices: expected float32 or float64 dtype");
                }

                auto copy_signed = [&](const auto *src) {
                    auto *dst = reinterpret_cast<uint32_t *>(mesh.triangles.data());
                    for (size_t i = 0; i < nt * 3; ++i) {
                        auto v = src[i];
                        if (v < 0 || static_cast<uint64_t>(v) > UINT32_MAX)
                            throw n3mf::InputError("triangles: vertex index out of uint32 range");
                        dst[i] = static_cast<uint32_t>(v);
                    }
                };

                if (tri_in.dtype() == nb::dtype<uint32_t>()) {
                    if (!is_c_contig(tri_in))
                        throw n3mf::InputError("triangles: array must be C-contiguous");
                    std::memcpy(mesh.triangles.data(), tri_in.data(),
                                nt * sizeof(n3mf::IndexTriangle));
                } else if (tri_in.dtype() == nb::dtype<int32_t>()) {
                    if (!is_c_contig(tri_in))
                        throw n3mf::InputError("triangles: array must be C-contiguous");
                    copy_signed(static_cast<const int32_t *>(tri_in.data()));
                } else if (tri_in.dtype() == nb::dtype<int64_t>()) {
                    if (!is_c_contig(tri_in))
                        throw n3mf::InputError("triangles: array must be C-contiguous");
                    copy_signed(static_cast<const int64_t *>(tri_in.data()));
                } else {
                    throw n3mf::InputError("triangles: expected uint32, int32, or int64 dtype");
                }

                return mesh;
            },
            "vertices"_a, "triangles"_a);

    // ---- BaseMaterial ------------------------------------------------------

    nb::class_<n3mf::BaseMaterial>(m, "BaseMaterial")
        .def(
            "__init__",
            [](n3mf::BaseMaterial *self, std::string name, n3mf::Color display_color) {
                new (self) n3mf::BaseMaterial{std::move(name), display_color};
            },
            "name"_a = "", "display_color"_a = n3mf::Color{})
        .def_rw("name", &n3mf::BaseMaterial::name)
        .def_rw("display_color", &n3mf::BaseMaterial::display_color)
        .def("__repr__",
             [](const n3mf::BaseMaterial &mat) { return "BaseMaterial('" + mat.name + "')"; });

    // ---- BaseMaterialGroup -------------------------------------------------

    nb::class_<n3mf::BaseMaterialGroup>(m, "BaseMaterialGroup")
        .def(nb::init<>())
        .def_rw("id", &n3mf::BaseMaterialGroup::id)
        .def_rw("materials", &n3mf::BaseMaterialGroup::materials)
        .def("__repr__", [](const n3mf::BaseMaterialGroup &g) {
            return "BaseMaterialGroup(id=" + std::to_string(g.id) +
                   ", count=" + std::to_string(g.materials.size()) + ")";
        });

    // ---- Metadata ----------------------------------------------------------

    nb::class_<n3mf::Metadata>(m, "Metadata")
        .def(
            "__init__",
            [](n3mf::Metadata *self, std::string name, std::string value, std::string type) {
                new (self) n3mf::Metadata{std::move(name), std::move(value), std::move(type)};
            },
            "name"_a = "", "value"_a = "", "type"_a = "")
        .def_rw("name", &n3mf::Metadata::name)
        .def_rw("value", &n3mf::Metadata::value)
        .def_rw("type", &n3mf::Metadata::type);

    // ---- XmlNamespace ------------------------------------------------------

    nb::class_<n3mf::XmlNamespace>(m, "XmlNamespace")
        .def(
            "__init__",
            [](n3mf::XmlNamespace *self, std::string prefix, std::string uri) {
                new (self) n3mf::XmlNamespace{std::move(prefix), std::move(uri)};
            },
            "prefix"_a = "", "uri"_a = "")
        .def_rw("prefix", &n3mf::XmlNamespace::prefix)
        .def_rw("uri", &n3mf::XmlNamespace::uri);

    // ---- Component ---------------------------------------------------------

    nb::class_<n3mf::Component>(m, "Component")
        .def(
            "__init__",
            [](n3mf::Component *self, uint32_t object_id, n3mf::Transform transform) {
                new (self) n3mf::Component{object_id, transform};
            },
            "object_id"_a = 0u, "transform"_a = n3mf::Transform::Identity())
        .def_rw("object_id", &n3mf::Component::object_id)
        .def_rw("transform", &n3mf::Component::transform);

    // ---- Object (read-only — created by DocumentBuilder) -------------------

    nb::class_<n3mf::Object>(m, "Object")
        .def_ro("id", &n3mf::Object::id)
        .def_ro("name", &n3mf::Object::name)
        .def_ro("type", &n3mf::Object::type)
        .def_ro("partnumber", &n3mf::Object::partnumber)
        .def_ro("components", &n3mf::Object::components)
        .def_ro("pid", &n3mf::Object::pid)
        .def_ro("pindex", &n3mf::Object::pindex)
        .def_ro("metadata", &n3mf::Object::metadata)
        .def_ro("component_transform", &n3mf::Object::component_transform)
        .def_ro("uuid", &n3mf::Object::uuid)
        .def("__repr__", [](const n3mf::Object &o) {
            return "Object(id=" + std::to_string(o.id) + ", name='" + o.name + "')";
        });

    // ---- BuildItem ---------------------------------------------------------

    nb::class_<n3mf::BuildItem>(m, "BuildItem")
        .def(
            "__init__",
            [](n3mf::BuildItem *self, uint32_t object_id, n3mf::Transform transform,
               std::string partnumber, std::string uuid) {
                new (self)
                    n3mf::BuildItem{object_id, transform, std::move(partnumber), std::move(uuid)};
            },
            "object_id"_a = 0u, "transform"_a = n3mf::Transform::Identity(), "partnumber"_a = "",
            "uuid"_a = "")
        .def_rw("object_id", &n3mf::BuildItem::object_id)
        .def_rw("transform", &n3mf::BuildItem::transform)
        .def_rw("partnumber", &n3mf::BuildItem::partnumber)
        .def_rw("uuid", &n3mf::BuildItem::uuid);

    // ---- Thumbnail ---------------------------------------------------------

    nb::class_<n3mf::Thumbnail>(m, "Thumbnail")
        .def(
            "__init__",
            [](n3mf::Thumbnail *self, nb::bytes data, std::string content_type) {
                auto ptr = reinterpret_cast<const uint8_t *>(data.c_str());
                new (self) n3mf::Thumbnail{std::vector<uint8_t>(ptr, ptr + data.size()),
                                           std::move(content_type)};
            },
            "data"_a, "content_type"_a)
        .def_prop_ro("data",
                     [](const n3mf::Thumbnail &t) {
                         return nb::bytes(reinterpret_cast<const char *>(t.data.data()),
                                          t.data.size());
                     })
        .def_rw("content_type", &n3mf::Thumbnail::content_type);

    // ---- CustomPart --------------------------------------------------------

    nb::class_<n3mf::CustomPart>(m, "CustomPart")
        .def(
            "__init__",
            [](n3mf::CustomPart *self, std::string path_in_zip, std::string content_type,
               nb::bytes data) {
                auto ptr = reinterpret_cast<const uint8_t *>(data.c_str());
                new (self) n3mf::CustomPart{std::move(path_in_zip), std::move(content_type),
                                            std::vector<uint8_t>(ptr, ptr + data.size())};
            },
            "path_in_zip"_a, "content_type"_a, "data"_a)
        .def_rw("path_in_zip", &n3mf::CustomPart::path_in_zip)
        .def_rw("content_type", &n3mf::CustomPart::content_type)
        .def_prop_ro("data", [](const n3mf::CustomPart &cp) {
            return nb::bytes(reinterpret_cast<const char *>(cp.data.data()), cp.data.size());
        });

    // ---- CustomRelationship ------------------------------------------------

    nb::class_<n3mf::CustomRelationship>(m, "CustomRelationship")
        .def(
            "__init__",
            [](n3mf::CustomRelationship *self, std::string source_part, std::string id,
               std::string type, std::string target) {
                new (self) n3mf::CustomRelationship{std::move(source_part), std::move(id),
                                                    std::move(type), std::move(target)};
            },
            "source_part"_a = "", "id"_a = "", "type"_a = "", "target"_a = "")
        .def_rw("source_part", &n3mf::CustomRelationship::source_part)
        .def_rw("id", &n3mf::CustomRelationship::id)
        .def_rw("type", &n3mf::CustomRelationship::type)
        .def_rw("target", &n3mf::CustomRelationship::target);

    // ---- CustomContentType -------------------------------------------------

    nb::class_<n3mf::CustomContentType>(m, "CustomContentType")
        .def(
            "__init__",
            [](n3mf::CustomContentType *self, std::string extension, std::string content_type) {
                new (self) n3mf::CustomContentType{std::move(extension), std::move(content_type)};
            },
            "extension"_a = "", "content_type"_a = "")
        .def_rw("extension", &n3mf::CustomContentType::extension)
        .def_rw("content_type", &n3mf::CustomContentType::content_type);

    // ---- Document::ProductionConfig ----------------------------------------

    nb::class_<n3mf::Document::ProductionConfig>(m, "ProductionConfig")
        .def(nb::init<>())
        .def_ro("enabled", &n3mf::Document::ProductionConfig::enabled)
        .def_ro("assembly_build_transform",
                &n3mf::Document::ProductionConfig::assembly_build_transform)
        .def_ro("merge_objects", &n3mf::Document::ProductionConfig::merge_objects)
        .def_ro("external_model_metadata",
                &n3mf::Document::ProductionConfig::external_model_metadata);

    // ---- Document ----------------------------------------------------------

    nb::class_<n3mf::Document>(m, "Document")
        .def(nb::init<>())
        .def_ro("unit", &n3mf::Document::unit)
        .def_ro("language", &n3mf::Document::language)
        .def_ro("metadata", &n3mf::Document::metadata)
        .def_ro("custom_namespaces", &n3mf::Document::custom_namespaces)
        .def_ro("base_material_groups", &n3mf::Document::base_material_groups)
        .def_ro("objects", &n3mf::Document::objects)
        .def_ro("build_items", &n3mf::Document::build_items)
        .def_ro("thumbnail", &n3mf::Document::thumbnail)
        .def_ro("custom_parts", &n3mf::Document::custom_parts)
        .def_ro("custom_relationships", &n3mf::Document::custom_relationships)
        .def_ro("custom_content_types", &n3mf::Document::custom_content_types)
        .def_ro("production", &n3mf::Document::production)
        .def("__repr__", [](const n3mf::Document &doc) {
            return "Document(unit=" + std::to_string(static_cast<int>(doc.unit)) +
                   ", objects=" + std::to_string(doc.objects.size()) +
                   ", build_items=" + std::to_string(doc.build_items.size()) + ")";
        });

    // ---- WriteOptions ------------------------------------------------------

    auto wo = nb::class_<n3mf::WriteOptions>(m, "WriteOptions");

    nb::enum_<n3mf::WriteOptions::Compression>(wo, "Compression")
        .value("Store", n3mf::WriteOptions::Compression::Store)
        .value("Deflate", n3mf::WriteOptions::Compression::Deflate)
        .value("Auto", n3mf::WriteOptions::Compression::Auto);

    wo.def(nb::init<>())
        .def_rw("compression", &n3mf::WriteOptions::compression)
        .def_rw("compression_threshold", &n3mf::WriteOptions::compression_threshold)
        .def_rw("deflate_level", &n3mf::WriteOptions::deflate_level)
        .def_rw("deterministic", &n3mf::WriteOptions::deterministic)
        .def_rw("compact_xml", &n3mf::WriteOptions::compact_xml);

    // ---- DocumentBuilder ---------------------------------------------------

    nb::class_<n3mf::DocumentBuilder>(m, "DocumentBuilder")
        .def(nb::init<>())

        .def("set_unit", &n3mf::DocumentBuilder::SetUnit, "unit"_a, nb::rv_policy::reference)

        .def("set_language", &n3mf::DocumentBuilder::SetLanguage, "lang"_a,
             nb::rv_policy::reference)

        .def("add_metadata", &n3mf::DocumentBuilder::AddMetadata, "name"_a, "value"_a,
             "type"_a = "", nb::rv_policy::reference)

        .def("add_namespace", &n3mf::DocumentBuilder::AddNamespace, "prefix"_a, "uri"_a,
             nb::rv_policy::reference)

        .def("add_base_material_group", &n3mf::DocumentBuilder::AddBaseMaterialGroup, "materials"_a)

        .def(
            "add_mesh_object",
            [](n3mf::DocumentBuilder &self, std::string name, n3mf::Mesh mesh,
               std::optional<uint32_t> pid, std::optional<uint32_t> pindex) -> uint32_t {
                return self.AddMeshObject(std::move(name), std::move(mesh), pid, pindex);
            },
            "name"_a, "mesh"_a, "pid"_a.none() = nb::none(), "pindex"_a.none() = nb::none())

        .def("add_component_object", &n3mf::DocumentBuilder::AddComponentObject, "name"_a,
             "components"_a)

        .def("set_object_type", &n3mf::DocumentBuilder::SetObjectType, "object_id"_a, "type"_a,
             nb::rv_policy::reference)

        .def("set_part_number", &n3mf::DocumentBuilder::SetPartNumber, "object_id"_a,
             "partnumber"_a, nb::rv_policy::reference)

        .def("add_object_metadata", &n3mf::DocumentBuilder::AddObjectMetadata, "object_id"_a,
             "name"_a, "value"_a, "type"_a = "", nb::rv_policy::reference)

        .def("set_component_transform", &n3mf::DocumentBuilder::SetComponentTransform,
             "object_id"_a, "transform"_a, nb::rv_policy::reference)

        .def("set_object_uuid", &n3mf::DocumentBuilder::SetObjectUUID, "object_id"_a, "uuid"_a,
             nb::rv_policy::reference)

        .def("add_build_item", &n3mf::DocumentBuilder::AddBuildItem, "object_id"_a,
             "transform"_a = n3mf::Transform::Identity(), "partnumber"_a = "", "uuid"_a = "",
             nb::rv_policy::reference)

        .def(
            "set_thumbnail",
            [](n3mf::DocumentBuilder &self, nb::bytes data,
               std::string content_type) -> n3mf::DocumentBuilder & {
                auto ptr = reinterpret_cast<const uint8_t *>(data.c_str());
                return self.SetThumbnail(std::vector<uint8_t>(ptr, ptr + data.size()),
                                         std::move(content_type));
            },
            "data"_a, "content_type"_a, nb::rv_policy::reference)

        .def("add_custom_part", &n3mf::DocumentBuilder::AddCustomPart, "part"_a,
             nb::rv_policy::reference)

        .def("add_custom_relationship", &n3mf::DocumentBuilder::AddCustomRelationship, "rel"_a,
             nb::rv_policy::reference)

        .def("add_custom_content_type", &n3mf::DocumentBuilder::AddCustomContentType, "ct"_a,
             nb::rv_policy::reference)

        .def("enable_production", &n3mf::DocumentBuilder::EnableProduction,
             "assembly_transform"_a = n3mf::Transform::Identity(), nb::rv_policy::reference)

        .def("set_production_merge_objects", &n3mf::DocumentBuilder::SetProductionMergeObjects,
             "merge"_a = true, nb::rv_policy::reference)

        .def("add_external_model_metadata", &n3mf::DocumentBuilder::AddExternalModelMetadata,
             "name"_a, "value"_a, "type"_a = "", nb::rv_policy::reference)

        .def("build", &n3mf::DocumentBuilder::Build);

    // ---- Writer functions --------------------------------------------------

    m.def(
        "write_to_buffer",
        [](const n3mf::Document &doc, const n3mf::WriteOptions &opts) {
            auto buf = n3mf::WriteToBuffer(doc, opts);
            return nb::bytes(reinterpret_cast<const char *>(buf.data()), buf.size());
        },
        "doc"_a, "opts"_a = n3mf::WriteOptions{});

    m.def("write_to_file", &n3mf::WriteToFile, "path"_a, "doc"_a, "opts"_a = n3mf::WriteOptions{});
}
