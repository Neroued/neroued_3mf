# neroued_3mf AGENTS 协作指南

本文件是仓库级导航入口，帮助开发者与 AI agent 快速定位模块、理解约束和执行常见任务。

## 1 分钟上手

1. 读总览：[README.md](README.md)
2. 按任务类型跳转到对应模块
3. 改代码前确认规则：`.cursor/rules/`
4. 改代码后执行格式化与文档同步检查

## 项目全景

neroued_3mf 是一个轻量 C++20 库，用于写入 3MF 文件（Phase 2 计划加入读取）。核心流程：

```
DocumentBuilder ──▶ Document ──▶ WriteToBuffer / WriteToFile / WriteToStream
 │
 ┌───────┴───────┐
 ▼               ▼
 StreamMeshXml   OPC metadata
 │               │
 └───────┬───────┘
         ▼
 StreamingZipWriter
         │
         ▼
 .3mf ZIP package
```

## 目录映射

- `include/neroued/3mf/`：公共 API 头文件
- `src/`：库实现（builder、writer、opc、zip）
- `src/internal/`：内部工具（xml_util、xml_stream_buffer、opc_types、validate）
- `tests/`：单元测试（GoogleTest）
- `cmake/`：CMake 配置模板（find_package 支持）
- `docs/`：设计文档

## 按任务快速定位

| 任务 | 首先查看 |
|------|----------|
| 新增材料类型（ColorGroup 等） | `include/neroued/3mf/materials.h`、`document.h`、`builder.h`、`src/opc.cpp` |
| 修改 XML 序列化逻辑 | `src/opc.h`（StreamMeshXml 模板）、`src/opc.cpp`（BuildOpcParts） |
| 修改 ZIP 压缩行为 | `src/zip.cpp`、`src/zip.h` |
| 修改 Builder API | `include/neroued/3mf/builder.h`、`src/builder.cpp` |
| 修改 Document 校验规则 | `src/internal/validate.h` |
| 修改写入流程 | `src/writer.cpp` |
| 添加自定义扩展支持 | `include/neroued/3mf/document.h`（CustomPart / CustomRelationship） |
| 修改 CMake 构建 / 安装 | `CMakeLists.txt`、`cmake/neroued_3mf-config.cmake.in` |
| 为 Reader 做准备 | `include/neroued/3mf/error.h`（FormatError）、`document.h`（Document 共享模型） |
| 修改核心几何类型 | `include/neroued/3mf/types.h`、`src/types.cpp` |
| 新增/修改测试 | `tests/test_types.cpp`、`tests/test_builder.cpp`、`tests/test_writer.cpp` |
| 添加缩略图支持 | `document.h`（Thumbnail）、`builder.h`、`opc.cpp`、`writer.cpp` |
| 修改核心组件支持 | `document.h`（Component）、`builder.h`、`opc.h`（StreamMeshXml）、`validate.h` |

## 核心类型与 API

### 公共头文件

| 文件 | 内容 |
|------|------|
| `neroued_3mf.h` | Umbrella header（include 全部公开头） |
| `version.h` | 编译期版本宏（CMake 生成） |
| `fwd.h` | 全部公共类型的前向声明 |
| `types.h` | `Vec3f`、`IndexTriangle`、`MeshView`、`Mesh`、`Color`、`Transform` |
| `materials.h` | `BaseMaterial`、`BaseMaterialGroup` |
| `document.h` | `Document`、`Object`、`BuildItem`、`Component`、`Thumbnail`、`ObjectType`、`CustomPart`/`CustomRelationship` |
| `builder.h` | `DocumentBuilder`（Builder 模式构建 Document） |
| `writer.h` | `WriteToBuffer`、`WriteToFile`、`WriteToStream`、`WriteOptions` |
| `error.h` | `InputError`、`IOError`、`FormatError` |

### 命名空间

- 公共 API：`neroued_3mf`
- 内部实现：`neroued_3mf::detail`
- 公共头文件使用 `<neroued/3mf/xxx.h>` 引用
- 内部头文件使用相对路径 `"subdir/xxx.h"` 引用

## Hooks（自动化执行）

以下行为由 `.cursor/hooks.json` 配置的 hooks 自动执行，无需手动操作：

- C++ 文件编辑后自动运行 `clang-format`（afterFileEdit hook）
- 危险 Shell 命令拦截：`rm -rf /`、`force push master` 等（beforeShellExecution hook）
- 敏感文件保护：`.env`、私钥、凭据文件等（beforeReadFile hook）
- Agent 结束时检查文档是否同步（stop hook）

## 全局协作规则

- C++ 代码格式化：[.cursor/rules/cpp-formatting.mdc](.cursor/rules/cpp-formatting.mdc)
- 工具函数复用与实现结构：[.cursor/rules/code-structure.mdc](.cursor/rules/code-structure.mdc)
- C++ 跨平台编译规范：[.cursor/rules/cross-platform-cpp.mdc](.cursor/rules/cross-platform-cpp.mdc)
- 文档同步规范：[.cursor/rules/sync-docs.mdc](.cursor/rules/sync-docs.mdc)
- Git 提交与合并规范：[.cursor/rules/git-policy.mdc](.cursor/rules/git-policy.mdc)

## 提交前检查清单

- [ ] C++ 文件执行 `clang-format -i <modified-files>`（已由 hook 自动执行）
- [ ] 构建通过：`cd build && cmake .. && cmake --build . -j$(nproc)`
- [ ] 测试通过：`cd build && ctest --output-on-failure`
- [ ] 公共 API 变更 → 更新 README.md API Overview
- [ ] Document 模型变更 → 更新 DESIGN.md 对应章节
- [ ] 配置参数增删 → 更新 README.md CMake Options
- [ ] 新增文件 → 更新 CMakeLists.txt 和 AGENTS.md 目录映射
- [ ] 不引入重复工具函数
- [ ] 不在公共头文件中暴露内部实现细节
