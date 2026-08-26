# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (c) 2026 neroued

"""neroued_3mf — lightweight Python bindings for writing 3MF files."""

from importlib.metadata import PackageNotFoundError as _PackageNotFoundError
from importlib.metadata import version as _distribution_version

from neroued_3mf._neroued_3mf import (
    BBox,
    BaseMaterial,
    BaseMaterialGroup,
    BuildItem,
    Color,
    Component,
    CustomContentType,
    CustomPart,
    CustomRelationship,
    Document,
    DocumentBuilder,
    IOError,
    IndexTriangle,
    InputError,
    Mesh,
    MeshAnalysis,
    Metadata,
    Object,
    ObjectType,
    ProductionConfig,
    Thumbnail,
    Transform,
    TriangleProperty,
    TriangleWriteMode,
    Unit,
    ValidationResult,
    Vec3f,
    WatermarkConfig,
    WatermarkResult,
    WriteOptions,
    XmlNamespace,
    analyze_mesh,
    detect_watermark,
    has_l2_signature,
    write_to_buffer,
    write_to_file,
)

try:
    __version__ = _distribution_version("neroued-3mf")
except _PackageNotFoundError:
    __version__ = "0+unknown"

__all__ = [
    "BBox",
    "BaseMaterial",
    "BaseMaterialGroup",
    "BuildItem",
    "Color",
    "Component",
    "CustomContentType",
    "CustomPart",
    "CustomRelationship",
    "Document",
    "DocumentBuilder",
    "IOError",
    "IndexTriangle",
    "InputError",
    "Mesh",
    "MeshAnalysis",
    "Metadata",
    "ObjectType",
    "Object",
    "ProductionConfig",
    "Thumbnail",
    "Transform",
    "TriangleProperty",
    "TriangleWriteMode",
    "Unit",
    "ValidationResult",
    "Vec3f",
    "WatermarkConfig",
    "WatermarkResult",
    "WriteOptions",
    "XmlNamespace",
    "analyze_mesh",
    "detect_watermark",
    "has_l2_signature",
    "write_to_buffer",
    "write_to_file",
]
