# 构建与验证

## 前置

- CMake ≥ 3.25
- Ninja
- C++20 编译器（本机验证 GCC 15.1.1 / Clang 20.1.8；分发配方 GCC 12 待基线 CI 构建）
- 默认固定依赖模式首次配置需联网；系统依赖模式不下载依赖，见下文。
- 图形 loader 生成需要 Python 3 与 Jinja2。
- 资产 PNG 解码需要系统 libpng 1.6 开发包（本机 1.6.50）；直接写入最终 RGBA 缓冲，避免完整解码副本。其 libpng/zlib 许可沿用系统依赖；stb 仍提供诊断 PNG 写出和 CPU 字体工具。
- 普通 GLFW 宿主需要 OpenGL 3.3 core 驱动、X11 和 Wayland/xkbcommon 开发包；构建同时启用两种窗口平台，启动自动选择当前会话。Qt/Plasma OpenGL 场景图接受 desktop GL 3.3 或 GLES 3.0，见 [Wayland 兼容](wayland-compatibility-2026-10-13.md)。
- ABI/普通播放器需要：pkg-config 和 libpulse ≥13.99 开发包（当前声音服务共享，不依赖显示服务器）
- 可选 Qt 宿主需要 Qt ≥6.4 Quick/OpenGL 开发组件；默认关闭。Plasma 的 KF6/Plasma 开发包仅诊断探针需要，分发构建可关闭 `NEKOMOTE_BUILD_PLASMA_PROBE`，见 [分发](plasma-distribution.md)。

## 使用发行版系统依赖

显式开启 `NEKOMOTE_USE_SYSTEM_DEPS`，也可选用继承 release 的 `system` preset：

```bash
cmake --preset system
cmake --build --preset system
# 同一开关也可用于其他宿主，例如仅 layer-shell：
cmake --preset layer-shell -DNEKOMOTE_USE_SYSTEM_DEPS=ON
```

系统模式只查询已安装的依赖，缺失即失败，不执行 FetchContent 或自动下载。
JSON 使用 `nlohmann_json` CMake config（≥3.11），fast_float 使用 `FastFloat` config，
stb 查找 `stb_image_write.h` 和 `stb_truetype.h`（兼容 `include/stb/`）。
普通窗口需要 GLFW≥3.4 的 `glfw3` config；启用测试才需要 Catch2≥3.5、major3。
OpenGL/ABI 使用系统 `glad` 2 CLI 生成 loader，`--reproducible` 读取该系统版本自带的
注册表，不向 Khronos 下载；不要求发行版额外打包 GLAD 的上游 CMake 辅助文件。

库与头文件来自系统前缀；CMake config / pkg-config 保留实际版本和链接信息。
系统库通常动态链接，header-only 库与生成的 loader 仍编译进程序。
自定义前缀可用标准 `CMAKE_PREFIX_PATH`，GLAD 可通过 PATH 或
`NEKOMOTE_GLAD_EXECUTABLE` 指定。切换依赖来源建议使用独立构建目录。

Plasma 的 `NEKOMOTE_INSTALL_QMLDIR` 是相对安装前缀的目录，默认 `qml`。
系统 Qt6 可设置 `lib/qt6/qml`，Qt5 对应设置 `lib/qt/qml`，插件 RPATH 按目录计算；
GitHub 归档继续沿用默认 `qml/` 和 `$ORIGIN/../../../lib`。

Arch 使用 [PKGBUILD](../packaging/arch/PKGBUILD)，构建方式和实际验证范围见
[打包说明](../packaging/arch/README.md)。其他发行版可复用同一 CMake 开关，系统包名
由其维护者选择。GitHub 工作流及固定版本入口不改用系统模式。

## Presets

| Preset | 类型 | 特性 | 预览 | 测试 |
| --- | --- | --- | --- | --- |
| `dev` | Debug | 警告即错误、compile_commands.json | ✓ | ✓ |
| `release` | Release | LTO、OpenGL + Vulkan 1.0、警告即错误 | ✓ | ✗ |
| `system` | Release | release 选项 + 发行版系统依赖，无自动下载 | ✓ | ✗ |
| `asan` | Debug | ASan + UBSan、警告即错误 | ✗ | ✓ |
| `qt-quick` | Release | LTO、OpenGL + Vulkan、可选Qt Quick OpenGL宿主 | ✓ | ✗ |
| `plasma` | Release | LTO、Plasma OpenGL/Vulkan 双栈及隔离探针 | ✓ | ✗ |
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

UI 必须链接 Fontconfig，通过系统/用户字体配置发现字体，不依赖固定字体安装路径。构建需要 Fontconfig 开发包（Debian/Ubuntu：`libfontconfig1-dev`），运行需要 Fontconfig 和可用的 TrueType/OpenType 字体；中文/日文界面建议安装 Noto CJK（Debian/Ubuntu：`fonts-noto-cjk`）。优先匹配覆盖中日英的 `Noto Sans CJK SC/TC/JP`、思源黑体 `Source Han Sans SC/TC/JP`；其后为 `Noto Sans`、`DejaVu Sans`、`Liberation Sans`、`Ubuntu`、`Cantarell`、系统 `sans-serif`，不要求某个特定字体必须存在。主字体查询带英文、简繁汉字及假名字形覆盖提示，实际四语 UI 和目录名称通过 `ui::prepare_menu_text` 在初始化/目录变更时准备。若首个匹配不能加载，会继续尝试其他候选；缺字时按字形回落到补充字体，度量与位图使用同一字体。全部失败时报告具体字体文件和原因。正常初始化只匹配和加载一个主字体；加载失败才查询候选列表。缺字时仅加载当前文本需要的补充字体。字体匹配、字形准备和上传发生在初始化/显式内容变更阶段，不进入稳态帧路径。

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
