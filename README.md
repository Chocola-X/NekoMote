# NekoMote

面向 Linux 原生动态壁纸的独立二维模型运行时项目。目标是复用已有模型素材，将动画求值、GPU 渲染与桌面环境接入解耦。

当前 P0/P1 已复核，P2 静态 GPU 基础已验证。原生固定姿态场景、网格、union 遮罩与透明颜色保护已实现：四型号初态及主模型缩放后的完整可见身体通过原 WebGL 图像对照。30 组网格/拓扑通过，最新严格图像对照为 25 PASS、5 FAIL（共 5 个轮廓像素）；现已接入原生主动作、线性表情路径、待机差分、固定作者时钟和实时窗口，仍为诊断入口；完整播放器尚未完成。时间线范围与剩余语义见 [P5 播放记录](docs/p5-native-animation.md)。当前进展、失败输入及性能范围见 [P4 静态合成记录](docs/p4-static-composition.md)，此前验证见 [原生场景记录](docs/p3-native-scene.md) 与 [P2 交接](docs/p2-handoff.md)。

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

联合动画与物理会话（验证进行中，完整播放器未认证）：

```sh
build/dev/bin/nekomote_preview --playback --asset /path/to/model.nkm.json \
  --origin 400 940 --play 首横振り 1
```

共同作者时钟、暂停和固定步进由 PlaybackInstance 管理。联合播放已支持模型比例（例如 `--scale 0.8`），缩放物理与几何通过、两组缩放差分截图仍有 2/1 个失败像素；静止/转头/24 步跳跃/60 步待机差分的原版完整物理、几何和截图对照通过；其他姿态仍有失败项。契约、完整失败输入、截图验证命令及性能范围见 [统一播放会话](docs/p6-playback-session.md)；后续关键帧权重修正见 [最新数值/画面证据](docs/validation/p6-world-deformation.json)。

模型比例、无效请求及重置契约、八组冻结缩放对照见 [缩放证据](docs/validation/p6-model-scaling.json)。

Fraise 已补齐子部件原点同步、细小发片细分和透明辅助遮罩，初态/横向转头/组合姿态通过；完整待机通过。纵向转头耳尖像素与两个完整差分会话的物理数值仍有 FAIL，详见 [跨型号证据与后续任务](docs/validation/p6-child-synchronization.json)。

最新精度复核：Chocola 四组与 Fraise 三组未修改源素材的完整会话均通过物理数值、最终几何和截图；不同作者/模型比例组合仍有明确 FAIL，参见 [当前证据](docs/validation/p6-transform-precision.json)。完整动画引擎与应用仍在开发中，不能将这七组对照视为全部能力验收。

固定姿态可导出实际 CPU 帧供逐三角形诊断：

```sh
build/dev/bin/nekomote_pose --asset /path/to/model.nkm.json \
  --variables '{"head_UD":30}' --dump-frame --output pose.json
```

`frame` 包含最终顶点 `[x,y,u,v]`、索引及部件的资源/索引范围，包含仅供遮罩使用的部件。
坐标是放置后的中心原点空间，UV 是纹理归一化坐标；用 `--sample-trace` 可采用冻结网页输入的位置与比例。
该导出仅属于离线诊断，不参与正式帧路径或性能采样。

链条绝对锚点已修复，Fraise 三组缩放物理数值均通过；图片失败仍保留，参见 [当前链条证据](docs/validation/p6-absolute-chain-anchors.json)。
