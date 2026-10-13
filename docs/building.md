# 构建与验证

## 前置

- CMake ≥ 3.25
- Ninja
- C++20 编译器（本机验证 GCC 15.1.1 / Clang 20.1.8；分发配方 GCC 12 待基线 CI 构建）
- 首次配置需联网（FetchContent 拉取依赖）
- 图形 loader 生成需要 Python 3 与 Jinja2。
- 资产 PNG 解码需要系统 libpng 1.6 开发包（本机 1.6.50）；直接写入最终 RGBA 缓冲，避免完整解码副本。其 libpng/zlib 许可沿用系统依赖；stb 仍提供诊断 PNG 写出和 CPU 字体工具。
- 普通 GLFW 宿主需要 OpenGL 3.3 core 驱动、X11 和 Wayland/xkbcommon 开发包；构建同时启用两种窗口平台，启动自动选择当前会话。Qt/Plasma OpenGL 场景图接受 desktop GL 3.3 或 GLES 3.0，见 [Wayland 兼容](wayland-compatibility-2026-10-13.md)。
- ABI/普通播放器需要：pkg-config 和 libpulse ≥16.1 开发包（当前声音服务共享，不依赖显示服务器）
- 可选 Qt 宿主需要 Qt ≥6.4 Quick/OpenGL 开发组件；默认关闭。Plasma 的 KF6/Plasma 开发包仅诊断探针需要，分发构建可关闭 `NEKOMOTE_BUILD_PLASMA_PROBE`，见 [分发](plasma-distribution.md)。

## Presets

| Preset | 类型 | 特性 | 预览 | 测试 |
| --- | --- | --- | --- | --- |
| `dev` | Debug | 警告即错误、compile_commands.json | ✓ | ✓ |
| `release` | Release | LTO、OpenGL + Vulkan 1.0、警告即错误 | ✓ | ✓ |
| `asan` | Debug | ASan + UBSan、警告即错误 | ✗ | ✓ |
| `qt-quick` | Release | LTO、OpenGL + Vulkan、可选Qt Quick OpenGL宿主 | ✓ | ✓ |
| `plasma` | Release | LTO、Plasma OpenGL/Vulkan 双栈及隔离探针 | ✓ | ✓ |
| `vulkan` | Debug | OpenGL + Vulkan 1.0、可用验证层 | ✓ | ✓ |

`asan` 不构建预览：GLFW + 图形驱动与 ASan 混用会产生大量非项目噪声，核心与测试路径已覆盖语义。

## 命令

```bash
# 配置
cmake --preset dev

# 构建
cmake --build --preset dev

# 测试
ctest --preset dev

# 运行预览（需显示服务器）
./build/dev/bin/nekomote_preview

# 格式检查（按 .clang-format）
clang-format --dry-run --Werror $(git ls-files '*.h' '*.hpp' '*.c' '*.cpp')
```

双栈需要 Vulkan 开发头与 `glslangValidator`，选择及同步契约见 [Vulkan 后端](vulkan-backend.md)。GPU 检查串行执行：`ctest --preset vulkan -L gpu -j1`。GPU 入口内部持有 `build/gpu-validation.lock`，不要在 CTest/Python 验证外再加同一把锁；直接运行 GPU 二进制时仍需 `flock`。

## 产物布局

```
build/<preset>/
  bin/nekomote_player       # 普通交互播放器
  bin/nekomote_preview      # 诊断预览
  bin/nekomote_unit_tests   # 单元测试可执行
  lib/libnekomote_model.a   # 核心静态库
  compile_commands.json     # IDE/clangd 索引
```

## 无显示服务器环境

核心库与单元测试不需要显示服务器：

```bash
cmake --preset dev -DNEKOMOTE_BUILD_PREVIEW=OFF -DNEKOMOTE_BUILD_PLAYER=OFF
cmake --build --preset dev
ctest --preset dev
```

## 缺少 refer/

`refer/` 被 `.gitignore` 忽略，是本地调查材料。正式构建不依赖它：缺失 `refer/` 不影响配置和构建，只影响显式素材调查工具（P1 起）。

## 当前资产与 P2 部件诊断

```bash
build/dev/bin/nekomote_import ../NekoWebShow/model_conversion/chocola/chocola-date-a/chocola-date-a.json \
  ../NekoWebShow/model_conversion/chocola/chocola-date-a/chocola-date-a.resx.json --output assets/characters/chocola-date-a
build/dev/bin/nekomote_preview --asset assets/characters/chocola-date-a/model.nkm.json \
  --icon tex/0003 --origin 400 651 --hidden --screenshot build/mouth.png
ctest --preset dev -L gpu  # 需要显示服务器，执行真实 GPU 像素/生命周期回归
```

上述入口为部件诊断。当前整模型普通窗口入口为 `nekomote_player`；导入会话、真实声音、完整适配和交互见 [原生播放器](native-player.md)。无显示环境运行 `ctest --preset dev -LE gpu`；`asan` 不构建 GPU 测试。参考截图命令见 [reference_capture/README.md](../tools/reference_capture/README.md)。

## 当前检查边界

ABI已与窗口解耦；asan包含CPU ABI合成契约，不构建player/预览/GPU。缺真实素材时相应集成项显式Skipped。C ABI诊断宿主不再依赖Pulse/thread，正式player保留声音服务。

UI测试和Python工具检查已注册CTest：

```sh
ctest --test-dir build/dev                    # 全部测试（含 player_ui、python_tools、python_abi_host）
ctest --test-dir build/dev -LE gpu           # 排除GPU测试（无显示服务器）
ctest --test-dir build/dev -L python         # 仅Python工具检查
ctest --test-dir build/dev -L gpu            # 仅GPU测试（player_ui、renderer_gpu、python_abi_host）
```

| 测试名 | 标签 | 说明 |
| --- | --- | --- |
| `player_ui` | gpu | 独立UI状态测试 |
| `python_tools` | python | 导入/资产保护与证据工具检查 |
| `python_abi_host` | python,gpu | ABI确定性测试（需abi_host与真实模型，缺失显式Skipped） |

缺 `assets/` 时 `python_abi_host` 显式 Skipped（`@unittest.skipUnless`），合成项仍执行。

Fontconfig为UI的可选字体发现入口，未安装时使用源码中的字体路径候选；无可用字体不能宣称UI成功。

## 构建边界

历史G9构建数字不代表当前全目录或当前测试总数。当前执行证据与未重跑范围见
[整合摘要](review-agents-current.md) 和 [Qt机器记录](validation/c7-qt-quick-results.json)。
ABI可独立于preview/player构建；`NEKOMOTE_BUILD_QT_QUICK=ON`要求ABI开启。
默认/asan关闭Qt选项，Qt依赖只由可选宿主目标引入。核心无显示服务器依赖。

源仓库只读；真实资产和截图放ignored assets/build目录，已跟踪的大型素材已移出索引。本批导入采用新输出目录，已有目录一律EXISTS_UNCHECKED、非零拒绝且保留，不覆盖或认证：

```sh
python3 tools/import_all_assets.py --reference-root ../NekoWebShow \
  --importer build/release/bin/nekomote_import \
  --session-tool tools/import_session/import_session.py \
  --output build/import-new/characters --report build/import-new/report.json
```

当前文件保护已修复，完整账本/异常/CPU结果保留已由G7实现；不将结构导入等同于画面通过。阶段验证范围见 [整合摘要](review-agents-current.md)。
