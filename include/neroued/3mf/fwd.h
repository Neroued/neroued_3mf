// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 neroued

#pragma once

/// \file fwd.h
/// \brief Forward declarations for all public neroued_3mf types.

namespace neroued_3mf {

struct Vec3f;
struct IndexTriangle;
struct TriangleProperty;
struct MeshView;
struct Mesh;
struct Color;
struct Transform;
struct BBox;
struct ValidationResult;

struct BaseMaterial;
struct BaseMaterialGroup;

enum class Unit;
enum class ObjectType;
struct XmlNamespace;
struct Metadata;
struct Component;
struct Object;
struct BuildItem;
struct Thumbnail;
struct CustomPart;
struct CustomRelationship;
struct CustomContentType;
struct Document;

class DocumentBuilder;

struct WatermarkConfig;
struct WriteOptions;
struct WatermarkResult;

class InputError;
class IOError;
class FormatError;

} // namespace neroued_3mf
