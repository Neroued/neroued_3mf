# neroued_3mf 设计文档

## 一、设计初衷

### 1.1 背景

neroued_3mf 从 [ChromaPrint3D](https://github.com/neroued/ChromaPrint3D) 的 3MF 导出模块中独立而来。原模块与 ChromaPrint3D 业务类型（`ModelIR`、`Channel`、`SlicerPreset`、`FaceOrientation` 等）深度耦合，公开 API 存在过度重载、职责混杂、数据拷贝等问题，且内嵌了 Bambu Studio 等 vendor 私有扩展逻辑。

独立为通用库的目标：

1. **标准化**：对标 3MF Core Specification v1.4，提供规范的类型与写入接口
2. **零依赖**：除 C++20 标准库和可选的 zlib 外，不引入任何第三方依赖
3. **高性能**：零拷贝网格引用 + 流式写入，峰值内存仅为外部网格数据 + ZIP 压缩缓冲
4. **vendor 中立**：库本身不包含任何 vendor 特化逻辑，通过通用接口注入私有元数据
5. **可复用**：任何需要生成 3MF 文件的 C++ 项目均可直接集成

### 1.2 与 lib3mf 的定位差异

[lib3mf](https://github.com/3MFConsortium/lib3mf) 是 3MF 联盟的官方 SDK，功能全面（读 + 写 + 完整扩展），但体量大、API 复杂。neroued_3mf 定位为 **轻量写入库**：

- 仅关注写入（Phase 2 计划加入读取）
- 极简 API（Builder → Document → Writer 三步完成）
- 零拷贝设计，适合嵌入对内存/性能敏感的管线
- 单一静态库，无运行时依赖

---

## 二、核心设计决策

### 2.1 双模式网格类型：MeshView + Mesh

| | MeshView | Mesh |
|---|---|---|
| 所有权 | 非拥有（`std::span`） | 拥有（`std::vector`） |
| 内存分配 | 零 | 管理自有缓冲 |
| 生命周期 | 调用方保证有效 | 自管理 |
| 工具方法 | 无 | Validate / RemoveDegenerateTriangles / Append / ComputeBoundingBox |
| 适用场景 | 性能路径——引用外部 meshing 库的输出缓冲 | 便利路径——库内构建、修改、持久持有 |

**决策理由**：3MF 导出通常处于管线末端，上游 meshing 库已经持有完整的顶点/索引数据。强制拷贝到库内部类型会浪费内存和时间。`MeshView` 通过 `std::span` 零拷贝引用外部缓冲，避免了这一问题。`Mesh` 则为不需要外部数据源的简单场景提供便利。

**内存布局兼容性**：

- `Vec3f` = `{float, float, float}` —— 与常见 3D 库的向量类型二进制兼容
- `IndexTriangle` = `{uint32_t, uint32_t, uint32_t}` —— 使用无符号索引，符合 3MF 规范

`Mesh` 可隐式转换为 `MeshView`，因此所有接受 `MeshView` 的 API 都自动兼容 `Mesh`。

### 2.1.1 有符号索引分析与“规范化 MeshView”

`MeshView` 保持一个明确约束：它只表达**已经规范化、可直接写出**的网格视图，即：

- 顶点：`span<const Vec3f>`
- 三角形：`span<const IndexTriangle>`（`uint32_t` 语义）
- 可选 `triangle_properties`

这意味着库**不会**再引入 `SignedMeshView`、callback mesh 或访问器风格的新主模型。对于调用方常见的 `int32_t` / `Vec3i` 三角索引缓冲，库新增的是**分析层能力**：

- `AsSignedIndexSpan()`：把 layout-compatible 的 signed triangle buffer 零拷贝映射成 `span<const int32_t>`
- `AnalyzeSignedTriangleBuffer()`：在不拷贝的前提下完成 bbox、越界计数、退化面计数和“实际可写三角形数”统计
- `AnalyzeMesh()`：对已经规范化的 `MeshView` / `Mesh` 复用同一套统计逻辑

这样做的核心原因是：负索引和越界索引属于**输入合法性问题**，而不是 `MeshView` 自身的语义问题。调用方先分析、再决定是否把同一块外部缓冲以 `MeshView` 形式交给 writer，就可以在保持 API 稳定的前提下实现零拷贝桥接。

### 2.1.2 Python NumPy 的显式零拷贝契约

Python 绑定不引入新的网格所有权模型，而是把满足布局要求的 NumPy CPU 数组直接映射为现有 `MeshView`：顶点必须是 C-contiguous `(N, 3)` `float32`，三角形必须是 C-contiguous `(M, 3)` `uint32` 或 `int32`，并满足底层 C++ 类型的对齐要求。NumPy/nanobind 依赖仍完全隔离在 `python/`，核心 C++ 库不感知 Python。

绑定提供两个入口，职责有意分开：

- `add_mesh_object()` 是兼容入口；布局匹配时零拷贝，否则沿用已有的单次转换路径
- `add_mesh_object_zero_copy()` 是性能契约入口；通过 nanobind 的 no-convert 参数约束拒绝任何需要 dtype、shape 或连续性转换的输入

两者最终仍调用同一个 `DocumentBuilder::AddMeshObject(MeshView)`，没有复制 writer、validation 或 Document 逻辑。`analyze_mesh()`、`MeshAnalysis`、`TriangleWriteMode` 和 `WriteOptions.triangle_write_mode` 同步暴露给 Python，使有符号索引预检和退化面过滤与 C++ 使用同一套语义。

零拷贝数组通过 `DocumentBuilder → Document` 的 Python 引用链保活；`Document` 销毁后引用才释放。这只解决对象生命周期，不固定缓冲地址：调用方在 Document 仍可写出期间不得 resize/reallocate 数组，也不得与 writer 并发修改数组。数组内容在写出时读取，因此写出前的原位修改会反映到结果中。

### 2.2 Document-first 流式写入（方式 A）

写入流程分两步：

1. **构建阶段**：通过 `DocumentBuilder` 构建一个轻量 `Document` 对象。Object 始终存储 `MeshView`；传入外部 `MeshView` 时 Document 不拥有网格数据，传入 `Mesh&&` 时则由 Document 的 `owned_meshes` 接管其生命周期。其余状态为元数据、材料定义、build items 和扩展注入点。
2. **写入阶段**：`WriteToBuffer` / `WriteToFile` / `WriteToStream` 读取 Document，通过 `StreamMeshXml`（模板化，编译期内联 sink 回调）将网格数据直接格式化到 64KB 固定缓冲区（`XmlStreamBuffer`），避免热路径上的堆分配，满时 flush 到 `StreamingZipWriter` 进行分块 deflate 压缩。

```
峰值内存 = 外部网格数据（调用方持有，库不拷贝）
           + XmlStreamBuffer 固定缓冲（64KB）
           + zlib deflate 工作缓冲（~128KB）
           + OPC 元数据 XML（~几 KB）
```

**热路径零堆分配**：顶点/三角形的浮点和整数格式化（`std::to_chars`）直接写入 `XmlStreamBuffer` 的栈缓冲区，消除了每元素 3-7 次 `std::string` 临时分配。

**紧凑 XML 模式**：`WriteOptions::compact_xml = true` 省略所有缩进空白，可减少 ~15-20% 的 XML 输出大小。

**可选退化面过滤**：`WriteOptions::triangle_write_mode` 默认为 `Keep`，保持历史行为不变；当显式设为 `FilterDegenerate` 时，writer 会在 XML 输出阶段跳过索引退化和面积退化三角形。过滤逻辑与分析 API 共用同一个内部实现（`src/internal/mesh_analysis.h`），从而保证：

- `triangle_properties` 与最终输出的 `<triangle>` 集合严格对齐
- watermark rotation table 的长度按**实际写出**的 triangle 数计算
- Production merged/per-object 路径的 `tri_offset` 累计与过滤后的输出一致

这使得调用方可以在保留现有 `MeshView` 语义的前提下，把“预检”和“实际写出”收敛到同一条语义路径上。

**未采用的备选方案（方式 B：完全流式）**：逐对象写入，允许写完一个对象后立即释放其网格数据。代价是 3MF XML 结构要求 `<resources>` 在 `<build>` 之前出现，完全流式需要更复杂的两趟写入或延迟 build 元素。方式 A 在实现复杂度和内存节省之间取得了更好的平衡。

### 2.3 vendor 中立：CustomPart / CustomRelationship 通用注入

库不内置任何 vendor 私有扩展（如 Bambu Studio 的 `Metadata/*.config`）。取而代之，提供三个通用注入接口：

- **`CustomPart`**：向 ZIP 包中添加任意文件（路径 + 内容类型 + 二进制数据）
- **`CustomRelationship`**：向 OPC 关系文件中添加自定义关系
- **`CustomContentType`**：注册自定义文件扩展名的内容类型

`CustomPart::data` 使用 `std::vector<uint8_t>` 以正确表示二进制内容语义。调用方（如 ChromaPrint3D）自行构造 vendor 私有数据的序列化内容，通过这些接口注入到最终的 3MF 包中。

**决策理由**：vendor 私有格式变化频繁，嵌入库内会导致耦合和维护负担。通用注入机制将 vendor 逻辑的拥有权完全交给调用方。

### 2.4 Builder 模式与自动 ID 管理

`DocumentBuilder` 提供流式 API，自动管理 3MF 资源 ID 的分配：

```cpp
auto mat_group = builder.AddBaseMaterialGroup(materials);  // 自动分配 ID
auto obj_id = builder.AddMeshObject("Part", mesh, mat_group, 0); // 自动分配 ID
builder.AddBuildItem(obj_id);
```

**所有权转移**：`AddMeshObject` 有两个重载——

- `AddMeshObject(name, MeshView, ...)` —— 纯引用，调用方负责生命周期
- `AddMeshObject(name, Mesh&&, ...)` —— Builder 接管 Mesh 所有权，存入内部 `owned_meshes_`

`Build()` 将所有 builder 内部状态 move 到 `Document` 中（含 `owned_meshes_`）。由于 `std::vector` 的 move 语义保留了元素地址不变，已有的 `MeshView` span 引用在 move 后仍然有效。调用 `Build()` 后 builder 设置 `built_` 标志，后续任何修改方法均抛出 `InputError`，确保不会误用已消耗的 builder。

`Document` 与 `DocumentBuilder` 均为 move-only。原因是 Object 中的 `MeshView` 可能指向同一对象内部的 owned mesh；默认浅拷贝会让副本继续引用原对象的存储，形成隐蔽的悬空引用风险。移动可保留底层 mesh 缓冲地址，又无需引入重绑定或共享所有权机制。

`Mesh::Append()` 在修改目标前完成索引范围和 `uint32_t` 溢出检查，并通过快照安全支持 self-append。双方已有 triangle 时，`triangle_properties` 布局必须一致（均为空，或均与各自 triangle 数量相同）；空目标可以直接采用来源布局。对于无法无损表达的混合布局直接抛出 `InputError`。

**对象属性设置**：Builder 采用"先创建、后配置"模式——`AddMeshObject` / `AddComponentObject` 返回 ID 后，通过 `SetObjectType`、`SetPartNumber`、`AddObjectMetadata`、`SetComponentTransform` 等方法按需设置属性。

**Document 一致性校验**：`Build()` 和 `Write*` 均会自动执行低成本的 Core 结构校验，不合规时抛出 `InputError`。校验项包括：

- Document 至少有一个 Object 和 BuildItem，Core 集合数量均小于 2^31
- Object 与材料组共享的 resource ID 全局唯一，且位于 `[1, 2^31)`
- Mesh 同时具有 vertices/triangles，至少有 3 个顶点，坐标有限且索引不越界
- `pid` / `pindex` 成对关系、材料组引用和索引范围有效；base material 的三角形属性不表达梯度
- `triangle_properties` 为空，或与 triangle 数量一致且 Object 提供有效的默认 `pid` / `pindex`
- Metadata 名称非空且同级唯一；带前缀的名称只使用已声明的非保留 namespace
- BuildItem 和 Component 的 transform 数值均为有限值
- Object 要么有 mesh 要么有 components（互斥，且至少有一个）
- Component 引用有效的已定义 Object，且无循环引用（DFS 检测任意深度循环）
- Thumbnail 数据非空，content_type 为 `image/png` 或 `image/jpeg`

这里刻意不做闭合性、流形、绕序、自交或自动修复等拓扑分析。`Keep` 模式只拒绝重复顶点索引这一 Core 硬性非法情况，仍保留索引互异但面积为零的三角形；`FilterDegenerate` 才会过滤两类退化面，并在某个 mesh 被过滤为空时拒绝写出。

### 2.5 Production Extension 支持

启用 Production 模式后，写入流程变为：

- 主模型文件 `3D/3dmodel.model` 包含一个 assembly object（`<components>` 引用外部 model）
- 每个 mesh object 写入独立的外部文件 `3D/Objects/object_N.model`（默认），或所有 object 合并到单个外部文件（`merge_objects = true`）
- OPC 关系文件注册主模型到外部模型的引用

调用方只需 `builder.EnableProduction(transform)` 即可启用。

**自定义 XML 命名空间**：`Document::custom_namespaces` 允许在所有 `<model>` 根元素上声明额外的 XML 命名空间（如 `xmlns:BambuStudio="..."`），装配 XML、外部对象 XML 和 flat model XML 均会输出这些声明。已被 Production 使用的保留前缀（`p`）在装配 XML 中会自动跳过。

**外部模型元数据**：`ProductionConfig::external_model_metadata` 允许在外部对象 `.model` 文件中注入独立的 `<metadata>` 节点（如 `BambuStudio:3mfVersion`），与主装配模型的 `Document::metadata` 相互独立。

**合并外部对象**：`ProductionConfig::merge_objects = true` 将所有对象写入同一个 `3D/Objects/object_1.model`，装配体的各 `<component>` 共享 `p:path`，以不同 `objectid` 区分。这与 Bambu Studio 等切片器输出的文件组织方式一致。

**组件变换**：`Object::component_transform` 指定该对象在 Production Extension 的 `<component>` 引用中使用的变换矩阵。

### 2.6 Core Spec Components

3MF 核心规范允许 Object 通过 `<components>` 引用其他 Object 构成装配体，无需 Production Extension。与 Production Extension 的外部文件引用不同，核心组件在同一 model 文件内通过 `objectid` 引用。

Object 可以是 mesh 对象（有顶点和三角形）或 component 对象（有 `<components>` 子元素引用其他对象），两者互斥。Builder 通过 `AddComponentObject(name, components)` 创建 component 对象。

### 2.7 Object 类型与元数据

每个 Object 具有 `ObjectType`（`Model` / `SolidSupport` / `Support` / `Surface` / `Other`）和可选的 `partnumber` 属性。Object 还可以包含 `<metadatagroup>` 下的对象级 `<metadata>` 元素。

`Metadata` 支持可选的 `type` 属性（如 `xs:string`），用于标注元数据值的类型。

### 2.8 缩略图

`Document::thumbnail` 支持在 3MF 包中嵌入一个 package 级缩略图（PNG 或 JPEG）。缩略图以 `Metadata/thumbnail.{png,jpeg}` 写入 ZIP，并在 package root 的 `.rels` 中注册 OPC thumbnail 关系。

### 2.9 双层水印

neroued_3mf 内置两层互补的水印机制，用于在不影响几何精度的前提下标识输出文件：

**L1：三角形索引旋转水印**

同一三角形 `(v1,v2,v3)` 有 3 种保持 CCW 法线方向不变的等价环形排列。利用「选哪种排列」这个自由度来隐蔽地编码信息（1 bit/三角形，仅使用 rotation 0 和 rotation 1）。

- 载荷封装为帧结构：`magic("N3MW") | flags(1B) | length(2B LE) | payload | CRC32(4B)`，overhead = 11 字节
- 帧比特流经 HMAC-SHA256 counter-mode 密钥流 XOR 加密后写入旋转表，无密钥时旋转序列统计上不可区分于随机
- 支持 3×/5× 重复冗余（多数投票纠错），三角形不足时自动降级：3× → 1× → 截断 → 静默跳过
- 预计算的旋转表在序列化阶段只读，与 OpenMP 并行完全兼容，每三角形额外开销 ~10 cycles
- **规范序（Canonical Ordering）**：编码器以词典序最小环形排列为基准施加旋转（`final_rot = (canon + wm) % 3`），使解码无需原始网格即可恢复载荷——解码器只需 `wm = (3 - CanonicalRotation(v1,v2,v3)) % 3`
- 规范序仅在水印启用时生效，无水印时 `rot=0`（identity），输出完全不变

**水印检测（Decoding Pipeline）**

独立的检测管线可从任意 3MF 文件恢复水印：

1. 轻量 ZIP 读取器（`zip_read.h`）解析 EOCD/CD，提取 model entry 数据（store/deflate）
2. XML 三角形扫描器（`ScanTrianglesFromXml`）简单状态机提取 `<triangle>` 标签的 `v1/v2/v3`
3. 对每个三角形计算规范旋转，反推 rotation bit
4. 调用 `DecodePayload` 恢复帧结构和载荷

公开 API：`DetectWatermark(data, key)` 返回 `WatermarkResult`（L2 签名 + L1 载荷），`HasL2Signature(data)` 纯二进制快速初筛。ZIP 读取独立于 Phase 2 Reader，不影响未来架构。

**L2：ZIP Extra Field 固定签名**

在 model entry（`3D/3dmodel.model` 或 `3D/Objects/*.model`）的 Local File Header 中写入固定 12 字节 extra field：Header ID `0x4E33`（不在 APPNOTE 已注册列表中）+ 8 字节 `"neroued\0"`。所有 3MF 阅读器会忽略未知 extra field。仅写入 Local Header，不写入 Central Directory，更隐蔽。此签名始终写入，不可配置。

### 2.10 原子文件写入

`WriteToFile` 使用 temp file + rename 模式：

1. 创建临时文件（同目录，随机后缀）
2. 写入全部数据
3. 原子 rename 到目标路径

避免了写入中途崩溃导致的文件损坏。Windows 下额外处理了目标文件已存在的情况。

---

## 三、分层架构

```
┌─────────────────────────────────────────────────────┐
│                  调用方（如 ChromaPrint3D）             │
│                                                     │
│  DocumentBuilder ──▶ Document ──▶ Write*             │
│                                                     │
│  CustomPart / CustomRelationship（vendor 注入）       │
└─────────────────────┬───────────────────────────────┘
                      │ 公开 API
╔═════════════════════╪═══════════════════════════════╗
║  neroued_3mf        │                               ║
║                     ▼                               ║
║  ┌──────────┐  ┌──────────┐  ┌───────────────────┐ ║
║  │ types.h  │  │builder.h │  │  document.h        │ ║
║  │ Mesh     │  │ Builder  │  │  Document/Object   │ ║
║  │ MeshView │  │          │  │  Component         │ ║
║  └──────────┘  └──────────┘  └───────────────────┘ ║
║                     │                               ║
║              ┌──────┴──────┐    内部实现             ║
║              ▼             ▼                        ║
║  ┌─────────────┐  ┌─────────────────────────────┐  ║
║  │  opc.cpp    │  │  StreamMeshXml              │  ║
║  │  OPC 元数据  │  │  逐批顶点/三角形 XML 流式化   │  ║
║  └──────┬──────┘  └──────────────┬──────────────┘  ║
║         │                        │                  ║
║         └───────────┬────────────┘                  ║
║                     ▼                               ║
║         ┌─────────────────────┐                     ║
║         │  StreamingZipWriter │                     ║
║         │  deflate / store    │                     ║
║         └─────────────────────┘                     ║
╚═════════════════════════════════════════════════════╝
```

**层次职责**：

| 层 | 文件 | 职责 |
|---|---|---|
| 公开 API | `types.h`, `materials.h`, `document.h`, `builder.h`, `writer.h`, `error.h` | 类型定义、Builder、写入入口 |
| 便利头文件 | `neroued_3mf.h`, `fwd.h`, `version.h` | Umbrella include、前向声明、编译期版本宏 |
| OPC 序列化 | `opc.h`（模板）, `opc.cpp` | `StreamMeshXml`（模板化零分配流式 XML）、`BuildOpcParts`（OPC 元数据） |
| XML 工具 | `internal/xml_util.h` | XML 转义、float 格式化、Transform 序列化 |
| 流式缓冲 | `internal/xml_stream_buffer.h` | `XmlStreamBuffer<Sink>`（64KB 固定缓冲，直接 `to_chars` 格式化） |
| ZIP 打包 | `zip.cpp/h` | StreamingZipWriter（deflate 流式压缩 + Data Descriptor + ZIP64）|
| 校验 | `internal/validate.h` | Document 一致性校验（O(1) 哈希查找，由 `Build()` 和 `Write*` 调用）|
| 网格分析 | `internal/mesh_analysis.h` | 共享 bbox / 越界 / 退化面判定，供 types / writer 复用 |
| OPC 类型 | `internal/opc_types.h` | 内部 OpcPart / OpcRelationship 结构体 |
| SHA-256 | `internal/sha256.h` | Header-only SHA-256 + HMAC-SHA-256（密钥流生成）|
| 水印核心 | `internal/watermark.h` | L1 编解码 + CanonicalRotation + ScanTrianglesFromXml |
| ZIP 读取 | `internal/zip_read.h` | 轻量 EOCD/CD 解析 + entry 提取（store/deflate），服务于水印检测 |
| 水印检测 | `watermark_detect.cpp` | DetectWatermark / HasL2Signature 实现（串联 ZIP 读取 + XML 扫描 + 规范序解码）|

---

## 四、与 3MF 标准的对标

### Phase 1（当前已实现）

| 3MF 特性 | 对应实现 |
|---|---|
| Core Spec v1.4: mesh, vertices, triangles | `MeshView` / `Mesh` → `StreamMeshXml` |
| Unit (micron ~ meter) | `Document::unit` / `Unit` enum |
| xml:lang | `Document::language` |
| Metadata (model-level, with type) | `Document::metadata` / `Metadata::type` |
| Per-object metadata (metadatagroup) | `Object::metadata` |
| Object type (model/solidsupport/support/surface/other) | `Object::type` / `ObjectType` enum |
| Object partnumber | `Object::partnumber` |
| Base Materials (`<basematerials>`) | `BaseMaterialGroup` |
| Object pid/pindex | `Object::pid` / `Object::pindex` |
| Per-triangle pid/p1/p2/p3 | `TriangleProperty` + `MeshView::triangle_properties` |
| Core-spec components (`<components>`) | `Object::components` / `Component` |
| Build items + transform + partnumber | `BuildItem` + `Transform` |
| Package thumbnail | `Document::thumbnail` / `Thumbnail` |
| Custom XML namespaces (all model roots) | `Document::custom_namespaces` |
| Production Extension (assembly + external models) | `Document::ProductionConfig` |
| Production 外部模型元数据 | `ProductionConfig::external_model_metadata` |
| Production 合并外部对象文件 | `ProductionConfig::merge_objects` |
| Production 组件变换 | `Object::component_transform` |
| OPC packaging (ContentTypes, Relationships) | `opc.cpp` |
| ZIP (store / deflate / ZIP64 / Data Descriptor) | `StreamingZipWriter` |
| ZIP64 (>4GB archives) | 自动启用 ZIP64 EOCD / Locator |
| 紧凑 XML 模式 | `WriteOptions::compact_xml` |
| 输入校验（退化三角形、索引越界） | `Mesh::Validate()` / `Mesh::RemoveDegenerateTriangles()` |
| Document 一致性校验 | `Build()` / `Write*` 自动校验 |
| 核心类型 `operator==` | `Vec3f` / `IndexTriangle` / `Color` / `Transform` / `TriangleProperty` |
| 三种写入方式 | `WriteToBuffer` / `WriteToFile` / `WriteToStream` |
| Vendor 扩展注入 | `CustomPart`（二进制数据）/ `CustomRelationship` / `CustomContentType` |
| Object type surface | `ObjectType::Surface` |
| Production UUID (p:UUID) | `Object::uuid` / `BuildItem::uuid` |
| Circular component reference detection (DFS) | `validate.h` — `DetectComponentCycles` |
| Build() state protection | `DocumentBuilder::built_` — 防止 Build 后误用 |
| L1 水印（三角形旋转） | `WatermarkConfig` / `BuildRotationTable` / `StreamMeshXml` |
| L2 水印（ZIP Extra Field） | `zip.cpp` — model entry Local Header 固定 12B 签名 |
| 水印检测（L1 + L2） | `DetectWatermark` / `HasL2Signature`（规范序解码，ZIP 读取 + XML 扫描）|

### Phase 2（规划中）

| 特性 | 说明 | 优先级 |
|---|---|---|
| **Materials Extension** | ColorGroup / Texture2D / Texture2DGroup / MultiProperties | 高 |
| **3MF Reader** | 解析 3MF 文件到 Document 模型 | 高 |
| **Triangle Sets** (v1.3) | 三角形分组，用于选区/颜色区域标记 | 中 |
| ~~**ZIP64**~~ | ~~突破 4GB 文件大小限制~~ | ✅ 已实现 |
| ~~**缩略图**~~ | ~~向 3MF 包中嵌入模型预览图~~ | ✅ 已实现 |
| **BeamLattice Extension** | 梁/格子结构，用于轻量化设计 | 低 |
| **Slice Extension** | 预切片数据嵌入 | 低 |
| **方式 B 流式写入** | 逐对象写入，支持写完即释放，适合超大模型 | 低 |

### Phase 2 扩展策略

新增 3MF 扩展时遵循的原则：

1. **公开类型先行**：在 `include/neroued/3mf/` 中定义资源类型（如 `ColorGroup`）
2. **Document 扩展**：在 `Document` 中添加对应的资源容器
3. **Builder 扩展**：在 `DocumentBuilder` 中添加构建方法（自动 ID 分配）
4. **OPC 序列化**：在 `opc.cpp` 中扩展 model XML 序列化逻辑
5. **测试覆盖**：补充对应的单元测试

不需要注册"扩展插件"或回调——直接在 Writer 核心逻辑中处理即可。vendor 私有内容继续通过 `CustomPart` 注入。

### Phase 2 Reader 预留策略

当前结构已为 Reader 做好预留：

- **Document 作为读写共享模型**：Reader 产出 `Document`（`owned_meshes` 容纳解析出的 Mesh，Object 的 MeshView 指向它们），Writer 消费 `Document`。这正是 Builder 的 owned mesh 路径已经在使用的模式。
- **FormatError 异常**：`error.h` 已预定义 `FormatError`，供 Reader 解析遇到格式错误时使用。
- **文件命名无冲突**：Reader 实现时新增 `reader.cpp`、`zip_reader.cpp/h`、`internal/xml_parser.h` 等，不与现有 write 文件冲突。
- **XML 解析策略**：计划内置一个极简 SAX-like parser（~300 行），仅支持 3MF 所需的 XML 子集，保持零依赖定位。

Reader 的预期公开 API：

```cpp
// include/neroued/3mf/reader.h (Phase 2)
Document ReadFromBuffer(std::span<const uint8_t> data, const ReadOptions& opts = {});
Document ReadFromFile(const std::string& path, const ReadOptions& opts = {});
```

---

## 五、外部集成

### 5.1 三种集成方式

**方式 A：add_subdirectory**

```cmake
add_subdirectory(third_party/neroued_3mf)
target_link_libraries(my_app PRIVATE neroued::3mf)
```

**方式 B：FetchContent**

```cmake
include(FetchContent)
FetchContent_Declare(neroued_3mf
    GIT_REPOSITORY https://github.com/neroued/neroued_3mf.git
    GIT_TAG v0.4.0)
set(N3MF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(neroued_3mf)
target_link_libraries(my_app PRIVATE neroued::3mf)
```

**方式 C：find_package（需先 install）**

```cmake
find_package(neroued_3mf 0.1 REQUIRED)
target_link_libraries(my_app PRIVATE neroued::3mf)
```

### 5.2 Umbrella Header

快速接入只需一行 include：

```cpp
#include <neroued/3mf/neroued_3mf.h>
```

此头文件包含所有公开类型、Builder、Writer 和版本信息。也可按需细粒度 include 单个头文件。

### 5.3 版本查询

编译期版本宏定义在 `<neroued/3mf/version.h>`（由 CMake 从 `version.h.in` 生成）：

```cpp
NEROUED_3MF_VERSION_MAJOR   // e.g. 0
NEROUED_3MF_VERSION_MINOR   // e.g. 4
NEROUED_3MF_VERSION_PATCH   // e.g. 0
NEROUED_3MF_VERSION_STRING  // e.g. "0.4.0"
```

---

## 六、从 ChromaPrint3D 迁移指南（后续阶段实施）

### 当前 ChromaPrint3D 中的适配路径（后续阶段实施）

```cpp
// ChromaPrint3D 适配层 (export_3mf.cpp)
neroued_3mf::DocumentBuilder builder;
builder.SetUnit(neroued_3mf::Unit::Millimeter);
builder.AddMetadata("Application", "ChromaPrint3D");

// 颜色调色板 → BaseMaterialGroup
uint32_t mat_group = builder.AddBaseMaterialGroup(ConvertPalette(palette));

// 现有 Mesh (ChromaPrint3D::Mesh) → MeshView 零拷贝
for (size_t i = 0; i < meshes.size(); ++i) {
    neroued_3mf::MeshView view{
        .vertices = std::span(
            reinterpret_cast<const neroued_3mf::Vec3f*>(meshes[i].vertices.data()),
            meshes[i].vertices.size()),
        .triangles = std::span(
            reinterpret_cast<const neroued_3mf::IndexTriangle*>(meshes[i].indices.data()),
            meshes[i].indices.size()),
    };
    auto obj_id = builder.AddMeshObject(names[i], view, mat_group, i);
    builder.AddBuildItem(obj_id);
}

// Bambu 扩展：ChromaPrint3D 自行构造、注入
if (preset.has_value()) {
    builder.EnableProduction(center_transform)
        .SetProductionMergeObjects(true);
    builder.AddNamespace("BambuStudio",
                         "http://schemas.bambulab.com/package/2021");
    builder.AddExternalModelMetadata("BambuStudio:3mfVersion", "1");

    std::string settings = BuildProjectSettings(preset);
    builder.AddCustomPart({"Metadata/project_settings.config",
                           "application/octet-stream",
                           {settings.begin(), settings.end()}});
    // ... 更多 custom parts / relationships
}

auto doc = builder.Build();
auto buffer = neroued_3mf::WriteToBuffer(doc);
```

**关键变化**：

| 原 ChromaPrint3D | 新 neroued_3mf |
|---|---|
| `ThreeMfInputObject` (name + hex_color + Mesh*) | `DocumentBuilder::AddMeshObject` (name + MeshView + pid/pindex) |
| `ThreeMfWriter` + `IThreeMfExtension` 插件机制 | 直接 `WriteToBuffer(doc)` / `WriteToFile(path, doc)` / `WriteToStream(out, doc)` |
| `BambuMetadataExtension` 内置于 Writer | 调用方通过 `AddCustomPart` 自行注入 |
| `ThreeMfDocument` 内部 IR | `Document` 公开类型 |
| `Mesh` 数据拷贝 | `MeshView` 零拷贝 |

---

## 七、仓库结构

```
neroued_3mf/
├── include/neroued/3mf/
│   ├── neroued_3mf.h   Umbrella header（include 全部公开头）
│   ├── version.h.in     版本宏模板（CMake configure_file 生成 version.h）
│   ├── fwd.h            前向声明（供仅需指针/引用的场景）
│   ├── types.h          Vec3f, IndexTriangle, MeshView, Mesh, Color, Transform
│   ├── materials.h      BaseMaterial, BaseMaterialGroup
│   ├── document.h       Document, Object, BuildItem, Component, Thumbnail, CustomPart/Relationship
│   ├── builder.h        DocumentBuilder
│   ├── writer.h         WriteToBuffer, WriteToFile, WriteToStream, WriteOptions
│   ├── watermark.h      WatermarkResult, DetectWatermark, HasL2Signature
│   └── error.h          InputError, IOError, FormatError
├── src/
│   ├── types.cpp        Mesh 工具方法实现
│   ├── builder.cpp      DocumentBuilder 实现（含 Document 校验）
│   ├── writer.cpp       顶层写入逻辑 + 原子文件写入
│   ├── watermark_detect.cpp  水印检测实现（DetectWatermark / HasL2Signature）
│   ├── opc.cpp/h        OPC 元数据 + StreamMeshXml
│   ├── zip.cpp/h        StreamingZipWriter
│   └── internal/
│       ├── opc_types.h  内部 OpcPart / OpcRelationship
│       ├── xml_util.h   XML 转义 / float 格式化
│       ├── xml_stream_buffer.h  XmlStreamBuffer（64KB 固定缓冲）
│       ├── validate.h   Document 一致性校验
│       ├── sha256.h     Header-only SHA-256 + HMAC-SHA-256
│       ├── watermark.h  L1 水印编解码 + CanonicalRotation + ScanTrianglesFromXml
│       └── zip_read.h   轻量 ZIP 读取（EOCD/CD 解析 + entry 提取）
├── cmake/
│   └── neroued_3mf-config.cmake.in  find_package 配置模板
├── tests/
│   ├── test_types.cpp   Mesh / MeshView / Color / Transform 测试
│   ├── test_builder.cpp DocumentBuilder 测试（含校验测试）
│   └── test_writer.cpp  端到端写入测试（含 Production 模式）
├── docs/
│   └── DESIGN.md        本文档
├── CMakeLists.txt
├── .clang-format
├── LICENSE              AGPL-3.0
├── COMMERCIAL_LICENSE.md
└── README.md
```

---

## 八、已知局限与未来工作

以下为当前版本已识别但尚未修复的问题，将在后续迭代中逐步解决：

1. **Materials Extension 未实现** — ColorGroup / Texture2D / Texture2DGroup / CompositeMaterials / MultiProperties 均不支持，仅有 BaseMaterial。这是 Phase 2 的高优先级项。
2. **Model XML 无扩展注入点** — 无法在 `<object>`、`<resources>`、`<build>` 内部注入自定义 XML 元素或属性；仅支持文件级（CustomPart）和元数据级（Metadata）扩展。
3. **Production 模式 Object 拷贝** — `writer.cpp` 中为 `StreamMeshXml` 构造临时 Document 时会拷贝 Object（不含 mesh 数据），对象多时有性能开销；根因是 `StreamMeshXml` 参数耦合了完整 Document 结构。
4. **OPC 装配 XML 非流式** — `opc.cpp` 的 Production 装配 XML 通过字符串拼接生成，不走 `XmlStreamBuffer`，`compact_xml` 选项对其无效。
5. **EscapeXml 始终分配** — `xml_util.h` 的 `EscapeXml` 即使无需转义也分配新字符串，热路径上可优化为先扫描后条件分配。
6. **Metadata preserve 属性** — 3MF 规范支持 `preserve="1"` 属性，当前未实现。
7. **通用 requiredextensions 管理** — 当前仅 Production 模式硬编码 `requiredextensions="p"`，无通用机制让调用方声明所需扩展。
8. **Reader 未实现** — ZIP 读取和 XML 解析均未实现，为 Phase 2 高优先级项。
