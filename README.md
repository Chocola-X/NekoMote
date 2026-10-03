# NekoMote

面向 Linux 原生动态壁纸的独立二维模型运行时项目。目标是复用已有模型素材，将动画求值、GPU 渲染与桌面环境接入解耦。

当前 P0/P1 已复核，P2 静态 GPU 基础已通过真实 WebGL 单部件图像对照：支持资产编译/加载、PNG 图集、OpenGL 3.3 部件预览与截图。P3 已加入原生固定姿态场景与网格诊断，通过四型号 30 组顶点/拓扑对照；完整图像仍有遮罩/合成差异，尚不能完整渲染或播放模型。进展见 [原生场景记录](docs/p3-native-scene.md)。验证见 [docs/p2-handoff.md](docs/p2-handoff.md)。

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
