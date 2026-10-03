# P0 阶段交接记录

阶段：P0 工程骨架与验证环境
日期：2026-10-03
状态：完成

## 目标

建立最小 CMake 工程、统一 presets、检查入口、无窗口核心库、独立预览与单元测试，为后续阶段提供可复现的构建与验证环境（ROADMAP §5）。

## 构建环境

| 项 | 实测值 |
| --- | --- |
| CMake | 4.1.1 |
| Ninja | 1.12.1 |
| GCC | 15.2.1 |
| Clang / clang-format | 20.1.8 |
| GPU | NVIDIA GeForce RTX 3080 Ti（驱动 580.82.09） |
| GL | OpenGL 4.6 可用，预览请求 3.3 core 成功 |
| 显示 | X11，DISPLAY=:0 |

> 注：本会话早期沙箱限制下 `/dev/dri` 不可见、GLX/EGL 均失败；切换自动模式后 GPU 恢复。预览已在硬件 GL 路径验证通过。

## 交付

- 根 `CMakeLists.txt`、`CMakePresets.json`（dev/release/asan）、`cmake/Dependencies.cmake`（FetchContent 固定版本）。
- `.clang-format`（按 AGENTS.md §5 约定）。
- 核心库 `nekomote_model`（静态库，仅依赖 C++ 标准库）：
  - `include/nekomote/types.hpp`：强类型 ID（NodeId/TextureId/ParamId/InstanceId/MeshId/TimelineId/SurfaceId）+ IdGenerator。
  - `include/nekomote/math.hpp`：Vec2、Mat3（列主序）、transform_point/direction。
  - `include/nekomote/units.hpp`：Seconds、FixedStep、Alpha、Radians、degrees_to_radians。
- 预览 `apps/preview`：GLFW + OpenGL 3.3 core，GPU 诊断输出，ESC/关窗退出，GLFW 错误回调。
- 单元测试 `tests/unit`：Catch2，17 个用例覆盖 ID/数学/单位。
- 文档：`docs/conventions.md`、`docs/dependencies.md`、`docs/building.md`、本文件。

## 依赖固定

| 依赖 | 固定版本 | 许可 |
| --- | --- | --- |
| nlohmann/json | v3.11.3 | MIT |
| stb | commit `2c980bb59875b0d32144a71867fbdebb2f77cd20` | Public Domain |
| Catch2 | v3.5.3 | BSL-1.0 |
| GLFW | 3.4 | zlib/libpng |

stb 无官方 tag，固定到具体 commit。所有依赖经 FetchContent 拉取，不依赖全局环境。

## 执行的命令与结果

```bash
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
# 162/162 编译，0 警告，17/17 测试通过

cmake --preset asan && cmake --build --preset asan && ctest --preset asan
# 160/160 编译，17/17 测试通过（ASan + UBSan）

cmake --preset release && cmake --build --preset release && ctest --preset release
# 162/162 编译（LTO），17/17 测试通过

clang-format --dry-run --Werror $(git ls-files '*.hpp' '*.cpp')
# 退出码 0，全部格式合规

./build/dev/bin/nekomote_preview
# 输出 GL_VENDOR/RENDERER/VERSION/GLSL，渲染诊断色，ESC/关窗退出
```

预览实测输出：
```
GL_VENDOR:   NVIDIA Corporation
GL_RENDERER: NVIDIA GeForce RTX 3080 Ti/PCIe/SSE2
GL_VERSION:  3.3.0 NVIDIA 580.82.09
GLSL:        3.30 NVIDIA via Cg compiler
```

## 退出条件核对

| 条件 | 状态 | 证据 |
| --- | --- | --- |
| 无窗口核心与独立预览分别构建 | ✓ | libnekomote_model.a 与 nekomote_preview 均产出 |
| 正式窗口/图形链显示诊断图形并正常退出 | ✓ | RTX 3080 Ti 上 GL 3.3 core 窗口渲染诊断色，ESC 退出路径已实现 |
| 空白场景生命周期无资源错误；ASan/UBSan 覆盖 | ✓ | asan preset 17/17 通过 |
| 缺 refer/ 仍能配置和构建 | ✓ | refer/ 被 .gitignore，构建未引用 |
| Presets 与检查命令可按文档复现 | ✓ | docs/building.md 记录全部命令 |

## 已验证语义

- 强类型 ID：不同 Tag 编译期隔离；IdGenerator 跳过 0；hash 稳定。
- 数学：Mat3 列主序，TRS 组合 = T*R*S，transform_point 含平移、transform_direction 不含。
- 单位：Seconds/Alpha/Radians 算术；FixedStep 有效性；度转弧度。

## 未验证假设 / 不支持能力

- model 空间原点与 Y 方向：待 P1 与真实素材核对，当前代码不假定朝向（conventions.md §1）。
- 颜色空间：默认 sRGB 输入/线性运算/sRGB 输出，待 P4 证据修正。
- 性能：P0 不承诺优化收益；无帧时间/资源基线（ROADMAP §5 工作项 7 仅设计格式，未实现记录器）。
- GL 函数加载为手写最小集；正式 loader（glad）在 P2 引入。
- 预览用 EGL context API（GLFW hint）：在 GLX 不可用环境作为 fallback，硬件环境亦验证通过。

## 当前接口契约

- `nekomote_model`：公共头 `include/nekomote/`，仅依赖 C++ 标准库。后续模块可链接 `nekomote::model`。
- 依赖不泄漏进核心公共头（AGENTS.md §3）。
- 无 C ABI（P8 桌面接入时再定义）。

## 删除的旧结构

无（P0 为初始建立）。

## 下一阶段（P1）首个具体任务

P1：素材审计、语义清单与导入器（ROADMAP §6）。

1. 从原仓库 `config/model-catalog.json`（195 条）建立型号 → PSB/JSON/resx/PNG/规则显式映射；处理别名与缺失（如 `milk-teenage.pure`、`milk-winter`）。
2. 以 `chocola-date-a` 为主样例：审计 mesh.bp/cc、parameterize、frameList、遮罩、继承字段，建立字段状态表（未调查/候选/已验证/不支持）。
3. 定义项目自己的强类型模型 schema（基于 `nekomote_model` 的 ID 体系），保留源字段路径与导入诊断。
4. 实现离线导入 CLI 骨架：显式输入根目录、型号、输出目录、能力报告。

最小复现：原仓库 `model_conversion/chocola/date-a/date-a.json` + 同目录 `.resx.json` 与资源目录，经导入器产出规范化资产与字段证据报告。
