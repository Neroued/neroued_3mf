# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 neroued

"""Basic functional tests for the neroued_3mf Python bindings."""

import tempfile
from pathlib import Path

import pytest

import neroued_3mf as n3mf


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def make_triangle_mesh():
    mesh = n3mf.Mesh()
    mesh.vertices = [n3mf.Vec3f(0, 0, 0), n3mf.Vec3f(10, 0, 0), n3mf.Vec3f(5, 10, 5)]
    mesh.triangles = [n3mf.IndexTriangle(0, 1, 2)]
    return mesh


def make_simple_document():
    builder = n3mf.DocumentBuilder()
    builder.set_unit(n3mf.Unit.Millimeter)
    obj_id = builder.add_mesh_object("Part1", make_triangle_mesh())
    builder.add_build_item(obj_id)
    return builder.build()


# ---------------------------------------------------------------------------
# Types
# ---------------------------------------------------------------------------

class TestVec3f:
    def test_construction(self):
        v = n3mf.Vec3f(1.0, 2.0, 3.0)
        assert v.x == pytest.approx(1.0)
        assert v.y == pytest.approx(2.0)
        assert v.z == pytest.approx(3.0)

    def test_default(self):
        v = n3mf.Vec3f()
        assert v.x == pytest.approx(0.0)

    def test_equality(self):
        assert n3mf.Vec3f(1, 2, 3) == n3mf.Vec3f(1, 2, 3)

    def test_is_finite(self):
        assert n3mf.Vec3f(1, 2, 3).is_finite()
        assert not n3mf.Vec3f(float("inf"), 0, 0).is_finite()

    def test_repr(self):
        assert "Vec3f" in repr(n3mf.Vec3f(1, 2, 3))


class TestColor:
    def test_construction(self):
        c = n3mf.Color(255, 128, 0, 200)
        assert c.r == 255
        assert c.g == 128
        assert c.b == 0
        assert c.a == 200

    def test_default_alpha(self):
        c = n3mf.Color(100, 100, 100)
        assert c.a == 255

    def test_hex_roundtrip(self):
        c = n3mf.Color(255, 0, 0, 255)
        hex_str = c.to_hex()
        c2 = n3mf.Color.from_hex(hex_str)
        assert c == c2

    def test_repr(self):
        assert "Color" in repr(n3mf.Color(1, 2, 3, 4))


class TestTransform:
    def test_identity(self):
        t = n3mf.Transform.identity()
        assert t.is_identity()

    def test_translation(self):
        t = n3mf.Transform.translation(10, 20, 30)
        assert not t.is_identity()
        assert t.m[9] == pytest.approx(10.0)
        assert t.m[10] == pytest.approx(20.0)
        assert t.m[11] == pytest.approx(30.0)


class TestMesh:
    def test_empty(self):
        mesh = n3mf.Mesh()
        assert mesh.empty
        assert mesh.vertex_count == 0
        assert mesh.triangle_count == 0

    def test_populate(self):
        mesh = make_triangle_mesh()
        assert mesh.vertex_count == 3
        assert mesh.triangle_count == 1
        assert not mesh.empty

    def test_bounding_box(self):
        mesh = make_triangle_mesh()
        bb = mesh.compute_bounding_box()
        assert bb.min.x == pytest.approx(0.0)
        assert bb.max.x == pytest.approx(10.0)

    def test_validate(self):
        mesh = make_triangle_mesh()
        result = mesh.validate()
        assert result.valid()
        assert result.degenerate_count == 0

    def test_append(self):
        m1 = make_triangle_mesh()
        m2 = make_triangle_mesh()
        m1.append(m2)
        assert m1.vertex_count == 6
        assert m1.triangle_count == 2

    def test_repr(self):
        assert "Mesh" in repr(make_triangle_mesh())


# ---------------------------------------------------------------------------
# DocumentBuilder
# ---------------------------------------------------------------------------

class TestDocumentBuilder:
    def test_basic_build(self):
        doc = make_simple_document()
        assert len(doc.objects) == 1
        assert len(doc.build_items) == 1
        assert doc.unit == n3mf.Unit.Millimeter

    def test_chaining(self):
        builder = n3mf.DocumentBuilder()
        result = builder.set_unit(n3mf.Unit.Meter).set_language("zh-CN")
        assert result is builder

    def test_materials(self):
        builder = n3mf.DocumentBuilder()
        mat_id = builder.add_base_material_group([
            n3mf.BaseMaterial("Red", n3mf.Color(255, 0, 0, 255)),
            n3mf.BaseMaterial("Blue", n3mf.Color(0, 0, 255, 255)),
        ])
        obj_id = builder.add_mesh_object("Part", make_triangle_mesh(), mat_id, 0)
        builder.add_build_item(obj_id)
        doc = builder.build()
        assert len(doc.base_material_groups) == 1
        assert doc.base_material_groups[0].id == mat_id

    def test_metadata(self):
        builder = n3mf.DocumentBuilder()
        builder.add_metadata("Application", "TestApp")
        obj_id = builder.add_mesh_object("Part", make_triangle_mesh())
        builder.add_build_item(obj_id)
        doc = builder.build()
        assert any(m.name == "Application" and m.value == "TestApp" for m in doc.metadata)

    def test_component_object(self):
        builder = n3mf.DocumentBuilder()
        mesh_id = builder.add_mesh_object("MeshPart", make_triangle_mesh())
        comp_id = builder.add_component_object("Assembly", [
            n3mf.Component(mesh_id, n3mf.Transform.identity()),
        ])
        builder.add_build_item(comp_id)
        doc = builder.build()
        assert len(doc.objects) == 2

    def test_no_build_items_throws(self):
        with pytest.raises(n3mf.InputError):
            n3mf.DocumentBuilder().build()

    def test_build_twice_throws(self):
        builder = n3mf.DocumentBuilder()
        obj_id = builder.add_mesh_object("P", make_triangle_mesh())
        builder.add_build_item(obj_id)
        builder.build()
        with pytest.raises(n3mf.InputError):
            builder.build()


# ---------------------------------------------------------------------------
# Writer
# ---------------------------------------------------------------------------

class TestWriter:
    def test_write_to_buffer(self):
        doc = make_simple_document()
        buf = n3mf.write_to_buffer(doc)
        assert isinstance(buf, bytes)
        assert buf[:2] == b"PK"
        assert len(buf) > 100

    def test_write_to_file_str(self):
        doc = make_simple_document()
        with tempfile.TemporaryDirectory() as tmp:
            path = str(Path(tmp) / "test.3mf")
            n3mf.write_to_file(path, doc)
            assert Path(path).stat().st_size > 100

    def test_write_to_file_pathlib(self):
        doc = make_simple_document()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.3mf"
            n3mf.write_to_file(path, doc)
            assert path.stat().st_size > 100

    def test_write_options(self):
        doc = make_simple_document()
        opts = n3mf.WriteOptions()
        opts.compression = n3mf.WriteOptions.Compression.Store
        opts.deterministic = True
        buf = n3mf.write_to_buffer(doc, opts)
        assert buf[:2] == b"PK"

    def test_deterministic_output(self):
        doc = make_simple_document()
        opts = n3mf.WriteOptions()
        opts.deterministic = True
        b1 = n3mf.write_to_buffer(doc, opts)
        b2 = n3mf.write_to_buffer(doc, opts)
        assert b1 == b2

    def test_empty_path_throws(self):
        doc = make_simple_document()
        with pytest.raises(n3mf.InputError):
            n3mf.write_to_file("", doc)


# ---------------------------------------------------------------------------
# WriteOptions.Compression enum
# ---------------------------------------------------------------------------

class TestWriteOptions:
    def test_compression_enum(self):
        assert n3mf.WriteOptions.Compression.Store is not None
        assert n3mf.WriteOptions.Compression.Deflate is not None
        assert n3mf.WriteOptions.Compression.Auto is not None

    def test_defaults(self):
        opts = n3mf.WriteOptions()
        assert opts.compression == n3mf.WriteOptions.Compression.Auto
        assert opts.deterministic is True
        assert opts.compact_xml is False
