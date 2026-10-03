# NekoMote

面向 Linux 原生动态壁纸的独立二维模型运行时项目。目标是复用已有模型素材，将动画求值、GPU 渲染与桌面环境接入解耦。

当前 P0（工程骨架与验证环境）和 P1（素材审计与导入器）已完成：已有最小 CMake 工程、核心库 `nekomote_model` / `nekomote_asset_io`、离线导入 CLI、GLFW 预览应用和 Catch2 单元测试。下一步为 P2（原生静态绘制基础）。

## 快速构建

需要 CMake ≥ 3.25、Ninja、C++20 编译器（GCC 或 Clang），首次配置需联网以拉取依赖。

```bash
cmake --preset dev              # 配置 Debug 构建
cmake --build --preset dev      # 编译核心库、预览和测试
ctest --preset dev              # 运行单元测试
./build/dev/bin/nekomote_preview  # 启动预览（需要 X11/显示服务器）
```

其他 preset：`release`（Release + LTO）、`asan`（Debug + ASan/UBSan，不构建预览）。

## 开发入口

- [分阶段开发路线图](ROADMAP.md)：模块结构、阶段任务、里程碑、风险与验收。
- [项目背景与开发规范](AGENTS.md)：项目目标、当前进度、模块边界、性能、生命周期及破坏性重构规则，同时作为 Agent 标准入口。
- [构建与验证](docs/building.md)：preset、命令、依赖与检查入口。
- [约定](docs/conventions.md)：坐标、矩阵、时间、alpha、ID 与所有权约定。

开发阶段以性能与代码规范性为优先约束，允许直接演进 API 和资产格式，不维护兼容层。
