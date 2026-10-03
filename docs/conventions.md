# NekoMote 约定

本文件是核心库（`nekomote_model` 及其公共头 `include/nekomote/`）所遵循的数值与所有权约定的权威说明。代码中散布的常量必须能在此找到理由。

## 1. 坐标空间

| 空间 | 原点 | Y 轴 | 范围 | 责任边界 |
| --- | --- | --- | --- | --- |
| model | 由导入资产决定 | 由导入资产决定 | 任意 | model/runtime 求值 |
| uv | 左上 | 向下 | `[0,1]^2` | asset_io 产出，render_gl 上传时翻转 |
| screen | 左上 | 向下 | 像素 | application/adapter 传入 |
| ndc | 中心 | 向上 | `[-1,1]^2` | render_gl 边界转换 |

**关键约束**：model 空间的原点与 Y 方向在 P1 与真实素材核对前不假定。任何依赖 model 空间朝向的代码必须标注待验证证据，不得凭字段名猜测。

## 2. 矩阵

- 存储列主序（与 OpenGL 一致）。`Mat3` 的 9 个 float 按 `[m00 m10 m20 | m01 m11 m21 | m02 m12 m22]` 排列。
- 元素访问 `m(row, col)`，`row`/`col` ∈ {0,1,2}。
- 变换组合：`M = T * R * S`，向量按列向量左乘：`v' = M * v`。
- 2D 仿射用 `Mat3`，齐次行 `[0 0 1]`。`transform_point` 用 `w=1`（含平移），`transform_direction` 用 `w=0`（仅线性部分）。

## 3. 时间

- 动画用单调时钟秒数（`Seconds`）。核心不假定显示 FPS。
- E-mote 的 60 帧基准由 runtime 在边界换算为 `Seconds`，常量不放进 `model`。
- 物理可用独立固定步长（`FixedStep`，正数秒），与显示 FPS 解耦。
- 暂停/恢复不补画历史帧；累计 delta 与追赶步数受限（AGENTS.md §7）。

## 4. Alpha 与颜色

- `Alpha` 为线性 `[0,1]` 浮点，0 全透、1 全不透。
- 核心存储直通 alpha；预乘 alpha 在 `render_gl` 边界统一施加。混用直通/预乘是渲染错误。
- 颜色空间在 P4 与素材核对后确定；当前默认 sRGB 输入、线性运算、sRGB 输出，待证据修正。

## 5. 角度

- 核心全程弧度（`Radians`）。度数只在导入/宿主边界出现，立即转换为 `Radians`。

## 6. 强类型 ID

- 所有实体 ID 用 `Id<Tag>` 包装，底层 `uint32_t`，0 为无效哨兵。
- `IdGenerator<Tag>::next()` 跳过 0，发出的 ID 恒有效。
- 不同 `Tag` 产生不同类型，编译期阻止混用（`NodeId` 不能赋给 `TextureId`）。
- ID 是稳定不透明值，不编码代数或槽位；生命周期与有效性由拥有容器负责。
- 已定义：`NodeId`、`TextureId`、`ParamId`、`InstanceId`、`MeshId`、`TimelineId`、`SurfaceId`。

## 7. 所有权

- 不可变 `ModelAsset` 与可变 `ModelInstance` 分离（AGENTS.md §6.2）。多个实例可共享同一 `ModelAsset`，但实例不共享隐含动画/物理状态。
- 默认 `std::unique_ptr` 或值语义。`shared_ptr` 仅用于有明确共享生命周期的对象。
- 裸指针与 `span` 表示非拥有视图，须注明有效期。
- GPU 对象在允许的线程/上下文释放；CPU 模型卸载与 GPU 资源释放分开（AGENTS.md §7）。

## 8. 命名与布局

- 类型 `PascalCase`，函数/变量 `snake_case`，成员 `snake_case_`，常量 `kPascalCase`，命名空间 `nekomote`。
- 文件 `snake_case.hpp/.cpp`，头文件 `#pragma once`。
- 4 空格缩进，行尾 LF，列宽 100。提交前以 `.clang-format` 输出为准。
