# NekoMote

面向 Linux 原生动态壁纸的独立二维模型运行时项目。目标是复用已有模型素材，将动画求值、GPU 渲染与桌面环境接入解耦。

当前 P0/P1 已复核，P2 静态 GPU 基础已验证。原生固定姿态场景、网格、union 遮罩与透明颜色保护已实现：四型号初态及主模型缩放后的完整可见身体通过原 WebGL 图像对照。30 组网格/拓扑通过，严格图像对照为 23 PASS、7 FAIL（共 10 个轮廓像素）；现已接入原生主动作、线性表情路径、待机差分、固定作者时钟和实时窗口，仍为诊断入口；完整播放器尚未完成。时间线范围与剩余语义见 [P5 播放记录](docs/p5-native-animation.md)。当前进展、失败输入及性能范围见 [P4 静态合成记录](docs/p4-static-composition.md)，此前验证见 [原生场景记录](docs/p3-native-scene.md) 与 [P2 交接](docs/p2-handoff.md)。

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

动画诊断（当前资产需重导入为 schema 5）：

```sh
build/dev/bin/nekomote_preview --animation-diagnostic --asset /path/to/model.nkm.json \
  --origin 400 940 --play sample_05 1 --play 待機ループ00 3
# 空格暂停/恢复，Esc 关闭。--steps N --hidden --screenshot frame.png 固定步进截图。
```

播放标志 0=替换、1=并行主动作、3=并行差分；这里明确隔离了物理和自动眨眼，不能当作完整壁纸产品。
