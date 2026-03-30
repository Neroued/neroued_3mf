# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 neroued

"""Basic functional tests for the neroued_3mf Python bindings."""

import tempfile
from pathlib import Path

import pytest

import neroued_3mf as n3mf

try:
    import numpy as np

    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False


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
        assert opts.vertex_precision == 9

    def test_watermark_config_on_write_options(self):
        opts = n3mf.WriteOptions()
        wm = n3mf.WatermarkConfig(payload=b"hello", key=b"secret", repetition=3)
        opts.watermark = wm
        assert opts.watermark.payload == b"hello"
        assert opts.watermark.key == b"secret"
        assert opts.watermark.repetition == 3

    def test_vertex_precision_reduces_size(self):
        mesh = n3mf.Mesh()
        mesh.vertices = [
            n3mf.Vec3f(1.23456789, 2.34567890, 3.45678901),
            n3mf.Vec3f(10.1234567, 0.00123456, 5.67890123),
            n3mf.Vec3f(5.55555555, 10.1010101, 0.99999999),
        ]
        mesh.triangles = [n3mf.IndexTriangle(0, 1, 2)]
        builder = n3mf.DocumentBuilder()
        obj_id = builder.add_mesh_object("Part", mesh)
        builder.add_build_item(obj_id)
        doc = builder.build()

        opts_full = n3mf.WriteOptions()
        opts_full.compression = n3mf.WriteOptions.Compression.Store
        opts_full.vertex_precision = 9
        buf_full = n3mf.write_to_buffer(doc, opts_full)

        opts_low = n3mf.WriteOptions()
        opts_low.compression = n3mf.WriteOptions.Compression.Store
        opts_low.vertex_precision = 3
        buf_low = n3mf.write_to_buffer(doc, opts_low)

        assert len(buf_low) < len(buf_full)
        assert buf_low[:2] == b"PK"


# ---------------------------------------------------------------------------
# Mesh.from_arrays (numpy)
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not HAS_NUMPY, reason="numpy not installed")
class TestMeshFromArrays:
    def test_float32_uint32(self):
        verts = np.array([[0, 0, 0], [10, 0, 0], [5, 10, 5]], dtype=np.float32)
        faces = np.array([[0, 1, 2]], dtype=np.uint32)
        mesh = n3mf.Mesh.from_arrays(verts, faces)
        assert mesh.vertex_count == 3
        assert mesh.triangle_count == 1
        assert mesh.vertices[0].x == pytest.approx(0.0)
        assert mesh.vertices[1].x == pytest.approx(10.0)
        assert mesh.triangles[0].v1 == 0
        assert mesh.triangles[0].v2 == 1
        assert mesh.triangles[0].v3 == 2

    def test_float64_int64(self):
        """Typical trimesh dtypes (float64 vertices, int64 faces)."""
        verts = np.array([[0, 0, 0], [10, 0, 0], [5, 10, 5]], dtype=np.float64)
        faces = np.array([[0, 1, 2]], dtype=np.int64)
        mesh = n3mf.Mesh.from_arrays(verts, faces)
        assert mesh.vertex_count == 3
        assert mesh.triangle_count == 1
        assert mesh.vertices[1].x == pytest.approx(10.0)
        assert mesh.triangles[0].v1 == 0

    def test_int32_triangles(self):
        verts = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32)
        faces = np.array([[0, 1, 2]], dtype=np.int32)
        mesh = n3mf.Mesh.from_arrays(verts, faces)
        assert mesh.triangle_count == 1

    def test_empty_arrays(self):
        verts = np.zeros((0, 3), dtype=np.float32)
        faces = np.zeros((0, 3), dtype=np.uint32)
        mesh = n3mf.Mesh.from_arrays(verts, faces)
        assert mesh.empty

    def test_wrong_vertex_shape_raises(self):
        verts = np.array([0, 0, 0, 10, 0, 0], dtype=np.float32)
        faces = np.array([[0, 1, 2]], dtype=np.uint32)
        with pytest.raises(n3mf.InputError):
            n3mf.Mesh.from_arrays(verts, faces)

    def test_wrong_triangle_shape_raises(self):
        verts = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32)
        faces = np.array([[0, 1, 2, 3]], dtype=np.uint32)
        with pytest.raises(n3mf.InputError):
            n3mf.Mesh.from_arrays(verts, faces)

    def test_negative_index_raises(self):
        verts = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32)
        faces = np.array([[-1, 1, 2]], dtype=np.int64)
        with pytest.raises(n3mf.InputError, match="out of uint32 range"):
            n3mf.Mesh.from_arrays(verts, faces)

    def test_wrong_vertex_dtype_raises(self):
        verts = np.array([[0, 0, 0]], dtype=np.int32)
        faces = np.array([[0, 1, 2]], dtype=np.uint32)
        with pytest.raises((n3mf.InputError, TypeError)):
            n3mf.Mesh.from_arrays(verts, faces)

    def test_wrong_triangle_dtype_raises(self):
        verts = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]], dtype=np.float32)
        faces = np.array([[0, 1, 2]], dtype=np.float64)
        with pytest.raises((n3mf.InputError, TypeError)):
            n3mf.Mesh.from_arrays(verts, faces)

    def test_large_mesh(self):
        rng = np.random.default_rng(42)
        nv, nt = 100_000, 200_000
        verts = rng.random((nv, 3)).astype(np.float64)
        faces = rng.integers(0, nv, size=(nt, 3)).astype(np.int64)
        mesh = n3mf.Mesh.from_arrays(verts, faces)
        assert mesh.vertex_count == nv
        assert mesh.triangle_count == nt

    def test_roundtrip_write(self):
        """Mesh from numpy arrays can be written to a valid 3MF ZIP."""
        verts = np.array([[0, 0, 0], [10, 0, 0], [5, 10, 5]], dtype=np.float64)
        faces = np.array([[0, 1, 2]], dtype=np.int64)
        mesh = n3mf.Mesh.from_arrays(verts, faces)

        builder = n3mf.DocumentBuilder()
        builder.set_unit(n3mf.Unit.Millimeter)
        obj_id = builder.add_mesh_object("Part", mesh)
        builder.add_build_item(obj_id)
        doc = builder.build()
        buf = n3mf.write_to_buffer(doc)
        assert buf[:2] == b"PK"
        assert len(buf) > 100

    def test_precision_preservation(self):
        """float64 -> float32 conversion preserves reasonable precision."""
        verts = np.array([[1.23456789, 2.34567890, 3.45678901]], dtype=np.float64)
        faces = np.array([[0, 0, 0]], dtype=np.uint32)
        mesh = n3mf.Mesh.from_arrays(verts, faces)
        assert mesh.vertices[0].x == pytest.approx(1.23456789, rel=1e-6)
        assert mesh.vertices[0].y == pytest.approx(2.34567890, rel=1e-6)


# ---------------------------------------------------------------------------
# Watermark
# ---------------------------------------------------------------------------

class TestWatermarkConfig:
    def test_defaults(self):
        wm = n3mf.WatermarkConfig()
        assert wm.payload == b""
        assert wm.key == b""
        assert wm.repetition == 3

    def test_construction(self):
        wm = n3mf.WatermarkConfig(payload=b"\x01\x02", key=b"\xaa\xbb", repetition=5)
        assert wm.payload == b"\x01\x02"
        assert wm.key == b"\xaa\xbb"
        assert wm.repetition == 5

    def test_mutable(self):
        wm = n3mf.WatermarkConfig()
        wm.payload = b"test"
        wm.key = b"key"
        wm.repetition = 1
        assert wm.payload == b"test"
        assert wm.key == b"key"
        assert wm.repetition == 1

    def test_repr(self):
        wm = n3mf.WatermarkConfig(payload=b"ab")
        assert "WatermarkConfig" in repr(wm)


class TestWatermarkResult:
    def test_default(self):
        r = n3mf.WatermarkResult()
        assert r.has_l2_signature is False
        assert r.has_l1_payload is False
        assert r.payload_truncated is False
        assert r.payload == b""

    def test_repr(self):
        r = n3mf.WatermarkResult()
        assert "WatermarkResult" in repr(r)


class TestWatermarkDetection:
    @staticmethod
    def _make_watermarked_3mf(payload=b"hello", key=b"secret"):
        """Write a 3MF with enough triangles and a watermark."""
        mesh = n3mf.Mesh()
        verts = []
        tris = []
        for i in range(1000):
            base = i * 3
            verts.extend([
                n3mf.Vec3f(float(i), 0.0, 0.0),
                n3mf.Vec3f(float(i) + 1.0, 0.0, 0.0),
                n3mf.Vec3f(float(i) + 0.5, 1.0, 0.0),
            ])
            tris.append(n3mf.IndexTriangle(base, base + 1, base + 2))
        mesh.vertices = verts
        mesh.triangles = tris

        builder = n3mf.DocumentBuilder()
        builder.set_unit(n3mf.Unit.Millimeter)
        obj_id = builder.add_mesh_object("Part", mesh)
        builder.add_build_item(obj_id)
        doc = builder.build()

        opts = n3mf.WriteOptions()
        opts.watermark = n3mf.WatermarkConfig(payload=payload, key=key)
        return n3mf.write_to_buffer(doc, opts)

    def test_roundtrip(self):
        buf = self._make_watermarked_3mf()
        result = n3mf.detect_watermark(buf, key=b"secret")
        assert result.has_l2_signature is True
        assert result.has_l1_payload is True
        assert result.payload == b"hello"

    def test_l2_signature(self):
        buf = self._make_watermarked_3mf()
        assert n3mf.has_l2_signature(buf) is True

    def test_no_watermark(self):
        doc = make_simple_document()
        buf = n3mf.write_to_buffer(doc)
        assert n3mf.has_l2_signature(buf) is True
        result = n3mf.detect_watermark(buf)
        assert result.has_l2_signature is True
        assert result.has_l1_payload is False

    def test_wrong_key(self):
        buf = self._make_watermarked_3mf(key=b"correct")
        result = n3mf.detect_watermark(buf, key=b"wrong")
        assert result.has_l2_signature is True
        assert result.payload != b"hello"

    def test_empty_data(self):
        assert n3mf.has_l2_signature(b"") is False
        result = n3mf.detect_watermark(b"")
        assert result.has_l2_signature is False
        assert result.has_l1_payload is False
