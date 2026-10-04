# NekoMote

面向 Linux 原生动态壁纸的独立二维模型运行时项目。目标是复用已有模型素材，将动画求值、GPU 渲染与桌面环境接入解耦。

当前已有独立的 C++ 模型求值、时间线/差分、变形、遮罩、物理和可操作窗口播放器，支持视线、触摸反应、真实声音/口型及 PNG 背景。Chocola、Vanilla、Fraise 的反应会话已导入验证。P0/P1/P2 完成；完整模型认证、Plasma 接入和全素材/长期运行仍在开发，严格 WebGL 对照中的几何/物理 FAIL 保留，未宣布最终交付。

启动普通播放器及操作说明见 [原生播放器](docs/native-player.md)。当前验证和失败记录见 [应用会话](docs/p6-application-plan.md)、[统一播放](docs/p6-playback-session.md) 与 [路线图](ROADMAP.md)。
## 快速构建

需要 CMake ≥ 3.25、Ninja、C++20 编译器（GCC 或 Clang），首次配置需联网以拉取依赖。普通播放器额外需要 libpulse 17.x 开发包。

```bash
cmake --preset dev              # 配置 Debug 构建
cmake --build --preset dev      # 编译核心库、预览和测试
ctest --preset dev              # 运行单元测试
./build/dev/bin/nekomote_player --help  # 普通播放器操作/素材参数
```

其他 preset：`release`（Release + LTO）、`asan`（Debug + ASan/UBSan，不构建预览）。

## 开发入口

- [分阶段开发路线图](ROADMAP.md)：模块结构、阶段任务、里程碑、风险与验收。
- [项目背景与开发规范](AGENTS.md)：项目目标、当前进度、模块边界、性能、生命周期及破坏性重构规则，同时作为 Agent 标准入口。
- [构建与验证](docs/building.md)：preset、命令、依赖与检查入口。
- [约定](docs/conventions.md)：坐标、矩阵、时间、alpha、ID 与所有权约定。

开发阶段以性能与代码规范性为优先约束，允许直接演进 API 和资产格式，不维护兼容层。

普通播放器使用自己的当前资产，不需要参考仓库参与运行：

```sh
build/release/bin/nekomote_player --model /path/model.nkm.json \
  --session /path/session.nks.json --fit --background /path/background.png
```

鼠标视线/左键触摸、右键拖动、滚轮缩放；空格暂停，F 适配完整模型，R 重置，Esc 关闭。没有声音服务时可显式 `--mute`。

`nekomote_preview`、`nekomote_playback_probe`、`nekomote_session_probe` 和 `nekomote_frame_render` 保留为独立诊断入口。固定步截图、数值、GPU 帧观察不得混入性能基准或替代正式模型求值。
