# NekoMote

<div align="center">
  <img src="./docs/branding/nekomote_logo.svg" alt="NekoMote" />
</div>

NekoMote 是 E-mote 二维角色模型的独立 C++ 实现，用于 Linux 桌面动态壁纸：模型求值、时间线动画、变形、遮罩、物理与 GPU 渲染均为从零实现的完整原生播放链条，不依赖浏览器、JavaScript、asm.js 或任何专有核心。目前可完美对接《猫娘乐园》（NEKOPARA）的角色解包素材（FreeMote 解包后经本项目导入器规范化），在真实桌面上长期稳定运行。

支持模型播放、视线/触摸反应、声音/口型、PNG 静态与分时段动态背景、物理摆动、网格变形与遮罩。渲染后端为 OpenGL 3.3 core（默认）或 Vulkan 1.0，可经 KDE Plasma 插件、Wayland/X11 独立壁纸壳或普通窗口运行。

许可证：GNU AGPL-3.0（见 [LICENSE](LICENSE)）。

## 功能

- 独立 C++ 模型求值核心：参数、时间线、差分、变形、遮罩、物理、视线/触摸反应
- OpenGL 3.3 与 Vulkan 1.0 双后端，运行时切换
- KDE Plasma 6 壁纸插件；独立 Plasma 5 适配器（Qt5）
- 独立 Wayland layer-shell 壁纸壳（OpenGL，面向支持该协议的合成器），见 [使用与验收范围](docs/layer-shell.md)
- 独立 X11/EWMH 壁纸壳（OpenGL/GLX，Ubuntu 20.04 基线），见 [使用与桌面共存限制](docs/x11.md)
- 普通窗口播放器（GLFW，X11 与 Wayland 自动选择）
- 声音与口型（PulseAudio），可静音
- PNG 静态背景与分时段动态背景（白天/黄昏/夜晚三相）
- 运行时 UI 控制面板：角色/背景切换、声音开关、FPS、锁定、位置调整
- 配置持久化：角色、背景、变换、UI 布局、静音状态写入 `config.json`

## 系统要求

构建：

- CMake ≥ 3.25、Ninja、C++20 编译器（GCC 或 Clang）
- Python 3 + Jinja2（GL loader 生成）
- libpng 1.6 开发包 + zlib
- pkg-config + libpulse ≥ 13.99 开发包（音频）
- 首次配置需联网（CMake FetchContent 拉取固定版本依赖）

运行时：

- OpenGL 3.3 core 驱动；Vulkan 后端另需 libvulkan（ICD loader）
- X11 或 Wayland 会话 + xkbcommon
- PulseAudio（音频；不可用时静音运行）
- Fontconfig 开发包（构建必需）及运行时可用的 TrueType/OpenType 字体；中文/日文界面建议安装 Noto CJK

可选：

- Vulkan SDK（headers + glslangValidator）——启用 Vulkan 后端构建
- Qt ≥ 6.4 Quick/OpenGL——Qt6 宿主与 Plasma6 插件
- Qt ≥ 5.12 Quick/OpenGL——独立 Plasma5 插件，显式选择 `NEKOMOTE_QT_MAJOR=5`
- KF6/Plasma 6 开发包——Plasma 诊断探针（分发构建可关闭）
- Wayland client ≥ 1.20、wayland-scanner、wayland-egl/cursor 与 EGL ≥ 1.5——独立 layer-shell 壳

FetchContent 自动拉取的固定依赖：nlohmann/json v3.11.3、fast_float v6.1.6、stb、FLAC 1.4.3、Catch2 v3.5.3、GLFW 3.4、glad 2.0.8。不进入核心库公共头文件。系统依赖模式（`-DNEKOMOTE_USE_SYSTEM_DEPS=ON`）要求系统提供等价版本（FLAC ≥ 1.4），缺失直接报错，不回退下载。

## 构建

```bash
cmake --preset release          # Release + LTO + Vulkan，发布构建
cmake --build --preset release
```

产物在 `build/release/bin/`：`nekomote_player`、`nekomote_import`、`nekomote_audio_convert`、`nekomote_model_convert`、`nekomote_png_recompress`、`nekomote_abi_host`、`nekomote_preview`（不含测试与诊断探针）。

其他 preset：

| Preset | 类型 | 特性 |
| --- | --- | --- |
| `dev` | Debug | 警告即错误，开发主力 |
| `system` | Release | 使用发行版系统依赖，无 FetchContent 下载；其他选项同 release |
| `release` | Release | LTO、OpenGL + Vulkan，不含测试/探针（发布构建） |
| `asan` | Debug | ASan/UBSan，不构建预览/播放器（无窗口语义检查） |
| `plasma` | Release | LTO、Qt6 + Plasma6 QML 模块；分发时关闭 KF6 探针 |
| `plasma5` | Release | LTO、Qt5 + Plasma5 QML 模块，不构建 KF6 探针/普通窗口 |
| `layer-shell` | Release | LTO、独立 Wayland 背景进程，仅 OpenGL，不依赖 Qt/GLFW |
| `x11` | Release | LTO、独立 X11/EWMH 背景进程，仅 OpenGL/GLX，不依赖 Qt/GLFW |
| `qt-quick` | Release | LTO、Qt Quick 隔离宿主（不需 Plasma 依赖） |
| `vulkan` | Debug | Vulkan + 验证层调试 |

无显示服务器环境构建核心与测试：

```bash
cmake --preset dev -DNEKOMOTE_BUILD_PREVIEW=OFF -DNEKOMOTE_BUILD_PLAYER=OFF
cmake --build --preset dev
ctest --test-dir build/dev -LE gpu
```

## 资产准备

资产分两类来源：角色模型与背景图片来自《猫娘乐园》（NEKOPARA）Steam 版游戏解包，经 FreeMote 解包后由本项目导入器规范化；动作反应会话（视线、触摸与动作规则）为用户自行配置，不属于游戏原始数据。

运行时只接受当前 schema 6 模型与 version 1 会话，不自动回退旧格式。资产放在 `assets/` 目录。

可直接从资产仓库 [NekoMoteAssets](https://github.com/Chocola-X/NekoMoteAssets) 获取已导入的现成资产包（角色模型、背景与图标），放入 `assets/` 目录即可使用；或按以下步骤从原始素材自行导入。

导入单个模型：

```bash
build/release/bin/nekomote_import \
  model.json model.resx.json --output assets/characters/<角色名>
```

导入会话（反应规则、视线、触摸区域）：

```bash
python3 tools/import_session/import_session.py \
  --reference-root <原解包素材根目录> \
  --model <角色名> --output assets/characters/<角色组>/<型号>/session.nks.json \
  --converter build/release/bin/nekomote_audio_convert \
  --character-root assets/characters/<角色组>
```

批量导入与背景：

```bash
python3 tools/import_all_assets.py --reference-root <原素材根目录> \
  --importer build/release/bin/nekomote_import \
  --session-tool tools/import_session/import_session.py \
  --output assets/characters --report assets/import-report.json
python3 tools/import_backgrounds.py --reference-root <原素材根目录> --dest assets
```

导入产物：

| 文件 | 用途 |
| --- | --- |
| `model.nkm` | schema 6 规范化模型，MessagePack + zlib 压缩容器 |
| `session.nks.json` | 反应会话 |
| `textures/N.png` | 引用的 PNG 图集 |
| `audio/main/*.flac` | 会话语音（FLAC 无损；`little/` 为小猫语音） |
| `report.json` | 结构状态与警告（非播放认证） |

已有 JSON/WAV 资产使用[一次性迁移工具](docs/asset-format.md#现有资产一次性迁移)转为当前格式。

背景放 `assets/backgrounds/`：静态 PNG 放根目录，动态背景放 `dynamic/` 子目录（A/B/C 三相分别对应白天/黄昏/夜晚）。

### 资产目录结构

```
assets/
├── config.json                       # 壁纸配置（角色、背景、语言、变换、UI、静音等，自动读写）
├── characters/
│   ├── characters.json               # 可选，角色显示名字典，键为 "组/变体"，值为多语言对象
│   └── <角色组>/                     # 一级目录：角色组（如 chocola）
│       ├── audio/main/               # 正常语音 FLAC（little/ 为小猫语音）
│       └── <变体>/                   # 二级目录：型号变体（如 date-a）
│           ├── model.nkm             # schema 6 压缩模型（必需）
│           ├── session.nks.json      # 反应会话（必需）
│           ├── meta.json             # 可选：cutoff_y / display_name / hidden / order
│           ├── audio -> ../audio/main # 按原声音 namespace 选择 main/little
│           ├── textures/             # PNG 图集
│           └── report.json           # 导入结构报告
├── backgrounds/
│   ├── background.json               # 可选，静态背景显示名字典（多语言对象）
│   ├── BG_07A.png                    # 静态背景（根目录，name 为文件 stem）
│   └── dynamic/
│       ├── background.json           # 可选，动态背景显示名字典（多语言对象）
│       └── <组名>/                   # 动态背景组（如 BG_01）
│           ├── config.json           # phases 数组 + transition_seconds
│           ├── A.png                 # 白天相
│           ├── B.png                 # 黄昏相
│           └── C.png                 # 夜晚相
└── icons/
    └── toggle.png                    # UI 面板图标
```

角色采用两层目录：`characters/<角色组>/<变体>/`，每个变体目录必须含 `model.nkm` 与 `session.nks.json`。角色名格式为 `组/变体`（如 `chocola/date-a`），用于 `--character` 参数与 `config.json` 的 `character` 字段。`characters.json` 提供显示名，键为完整角色名。

显示名字典（`characters.json`、`background.json`、`dynamic/background.json`、`meta.json` 的 `display_name`）支持两种值格式：多语言对象按语言代码（`en` / `zh-CN` / `zh-TW` / `ja`）给出翻译；纯字符串则对所有语言生效。运行时按 `config.json` 的 `language` 字段选取，缺失的语言按 zh-CN → en → zh-TW → ja 顺序回退，最后回退到资产 ID。UI 面板语言在同一字段中切换，首次启动（无 `config.json`）默认英文。

静态背景的 `name` 为 PNG 文件 stem（如 `BG_07A`）。动态背景的 `name` 为组目录名（如 `BG_01`），其 `config.json` 定义各相图片与切换时间：

```json
{
  "phases": [
    {"image": "A.png", "start": "06:00"},
    {"image": "B.png", "start": "16:00"},
    {"image": "C.png", "start": "18:00"}
  ],
  "transition_seconds": 1.8
}
```

`start` 为 24 小时制 HH:MM，按当天分钟数排序选择当前相。`meta.json` 的 `cutoff_y` 为角色裁切 Y 坐标，`hidden` 为 `true` 时不在 UI 列表显示，`order` 控制组内排序。

## 运行播放器

```bash
# 目录模式：从 assets/ 加载，UI 可切换角色/背景
build/release/bin/nekomote_player --assets assets --character <角色名>

# 单模型模式
build/release/bin/nekomote_player \
  --model assets/characters/<角色名>/model.nkm \
  --session assets/characters/<角色名>/session.nks.json --fit

# 无音频环境
build/release/bin/nekomote_player --assets assets --character <角色名> --mute
```

常用参数：`--backend opengl|vulkan`、`--fit`、`--scale`、`--origin-x/--origin-y`、`--width/--height`、`--fps`、`--seed`、640×64 范围。完整列表见 `--help`。

操作：

- 鼠标移动：视线跟随；左键短按释放：触摸；左键长按 300ms 后：拖动角色；滚轮：围绕指针缩放
- 空格：暂停/继续；F：适配完整模型；R：重置角色；Esc：关闭
- Tab / 1–9：切换角色；B / N：切换背景；C：清除背景
- 右上角图标：打开 UI 控制面板（角色、背景选择、语言、关闭声音、FPS、锁定、重设位置）

声音开关统一由 UI 控制面板的"关闭声音"管理，状态写入 `config.json` 的 `mute` 字段，下次启动自动恢复。`--mute` 仅作启动时强制静音覆盖。

界面与资产显示名支持英文、简体中文、繁体中文与日文，在 UI 控制面板的设置抽屉中切换并写入 `config.json` 的 `language` 字段。首次启动（资产目录无 `config.json`）默认英文，并自动选取列表中的第一张背景作为初始壁纸；已有配置中显式清空的背景保持无背景状态。

## KDE Plasma 壁纸部署

### 从源码构建插件

```bash
cmake --preset plasma -DNEKOMOTE_BUILD_PLASMA_PROBE=OFF
cmake --build --preset plasma
```

`NEKOMOTE_BUILD_PLASMA_PROBE=OFF` 关闭需要 KF6/Plasma 开发包的本地探针，仅构建可分发的 QML 模块与插件。产物在 `build/plasma/adapters/plasma/`。

### 安装到桌面

使用统一部署工具（`install`/`reinstall`/`uninstall`/`status`/`verify`）：

```bash
python3 tools/plasma_deploy.py install --stage build/plasma/adapters/plasma/stage
```

安装到 `$XDG_DATA_HOME/nekomote/`（默认 `~/.local/share/nekomote/`），自动写入 `environment.d` 的 `QML_IMPORT_PATH` 并刷新 KPackage 缓存。注销重新登录后生效；要立即生效加 `--restart-shell`。

安装后在 Plasma 桌面右键 → 配置壁纸 → 选择 NekoMote，在壁纸配置中设置 assets 目录路径。

### 分发包

发行版维护者可用 `-DNEKOMOTE_USE_SYSTEM_DEPS=ON`（或 `cmake --preset system`），
使用系统安装的依赖版本；缺少依赖直接报错，不下载固定版本。
默认构建与 GitHub Actions 继续使用原有固定依赖。
Arch 的三包 PKGBUILD 见 [打包说明](packaging/arch/README.md)。

五个独立 GitHub Actions 工作流在分支 push、PR 或手动触发时分别构建：

- Ubuntu 20.04：x86_64 / ARM64 的 Release `nekomote_player`、`nekomote_import`、`nekomote_audio_convert`、`nekomote_model_convert`、`nekomote_png_recompress`、`nekomote_abi_host`。
- KDE5：Ubuntu 20.04 / Qt5.12.8 的 x86_64 与 ARM64 插件。
- KDE6：Debian 12 / Qt6.4.2 的 x86_64 与 ARM64 插件。
- Layer-shell：Debian 12 / Wayland 1.21 的 x86_64 与 ARM64 独立程序。
- X11：Ubuntu 20.04 / EWMH/GLX 的 x86_64 与 ARM64 独立程序。

构建、测试与依赖审计通过后上传 Actions artifacts；失败日志与测试报告单独保存。
推送 `v0.5.0` 这样的 `vX.Y.Z` 稳定版本标签后，发布工作流会构建上述全部平台，
校验十份归档及 SHA256，自动发布到 GitHub Releases。
归档不附带 README；项目许可证为 AGPL-v3，依赖必须保留的声明合并在
`THIRD_PARTY_NOTICES`。配置和兼容范围见 [自动构建说明](docs/github-actions.md)。目标机器安装前先检查：

```bash
tar -xzf nekomote-plasma6-x86_64.tar.gz
cd nekomote-plasma
python3 tools/plasma_distribution.py check-host stage
python3 tools/plasma_deploy.py install --stage stage
```

插件使用宿主 Qt 公共 Quick API，发行包不附带 Qt/KDE/glibc/GPU 驱动。一个二进制不能覆盖所有 Linux 发行版：Qt 插件要求相同 major、不低于构建 minor 的 Qt。详见 [Plasma 分发](docs/plasma-distribution.md)。Plasma 5 包（`nekomote-plasma5-*`）步骤相同，但必须装进 Plasma 5 桌面，不得装进 Plasma 6。

## 独立壁纸壳（Wayland layer-shell / X11）

不使用 Plasma 插件时，可经两个独立进程壳把同一壁纸本体接到其他桌面。两者都只通过 C ABI 复用共享的模型、背景、声音、控制面板与配置，不重复任何业务逻辑：

- **`nekomote_layer_shell`**：面向实现 [wlr-layer-shell](https://gitlab.freedesktop.org/wlroots/wlr-protocols/-/blob/master/unstable/wlr-layer-shell-unstable-v1.xml) 协议的 Wayland 合成器（Sway、Hyprland、niri 等），OpenGL/EGL 输出。
- **`nekomote_x11`**：面向任意实现 EWMH 的 X11 窗口管理器，以桌面层窗口（`_NET_WM_WINDOW_TYPE_DESKTOP`）常驻，OpenGL/GLX 输出，Ubuntu 20.04 基线。

### 构建与运行

```bash
# Wayland layer-shell 壳（需 Wayland client ≥ 1.20、wayland-scanner、EGL ≥ 1.5）
cmake --preset layer-shell && cmake --build --preset layer-shell
build/layer-shell/bin/nekomote_layer_shell --list-outputs
build/layer-shell/bin/nekomote_layer_shell --assets assets --output DP-1

# X11/EWMH 壳（需 X11、RandR ≥ 1.3、GLX 开发文件）
cmake --preset x11 && cmake --build --preset x11
build/x11/bin/nekomote_x11 --list-outputs
build/x11/bin/nekomote_x11 --assets assets --output HDMI-1
```

两者命令行契约基本一致：

| 参数 | 含义 |
| --- | --- |
| `--assets DIR` | 正式资产目录，共用角色/背景扫描、切换及控制面板 |
| `--output NAME` | 绑定指定输出；该输出移除后进程正常退出。不指定时 layer-shell 由合成器选择、X11 取主输出 |
| `--input interactive` | 默认；背景可见处接收本地指针，转发共享 UI 与模型交互 |
| `--input passthrough` | 空输入区域，全部穿透；无法操作控制面板与触摸交互 |
| `--config FILE` | 显式指定该实例配置文件 |
| `--mute` / `--no-save` | 强制静音 / 不写配置 |
| `--model FILE [--session FILE]` | 单模型诊断入口，不绘制资产控制面板 |
| `--frames N [--capture FILE.ppm]` | 固定 1/60 秒步长诊断回放，仅在最后一步读一次 framebuffer |

X11 壳另支持 `--ui-open`，在交互模式启动时展开控制面板。

多屏分别为每个输出启动一个进程并显式指定 `--output`；建议次要屏幕加 `--mute` 避免各实例同时发声。同一输出不要同时运行多个壁纸程序。每实例配置独立存放于 `$XDG_CONFIG_HOME/nekomote/<壳名>/output-<输出名>.json`。

两个壳默认关闭，不依赖 Qt/GLFW；协议能力缺失（无 layer-shell 的合成器、无 EWMH 支持的 WM）时明确失败退出，不退化为普通窗口。适用范围与验收边界见 [layer-shell 说明](docs/layer-shell.md)与 [X11 说明](docs/x11.md)（后者含 XFCE 桌面图标共存的已知限制）。

## 配置

`assets/config.json` 由播放器/壁纸自动读写，字段：

| 字段 | 说明 |
| --- | --- |
| `character` | 当前角色名 |
| `background` | 当前背景名（静态为文件 stem，动态为组名） |
| `scale`、`origin_x`、`origin_y` | 角色缩放与中心原点位置（Y 向下） |
| `width`、`height` | 输出尺寸 |
| `fps` | 显示帧率限制（作者时钟仍 60Hz） |
| `mute` | 静音状态（由 UI 面板维护） |
| `fit` | 适配完整模型 |
| `icon_x`、`icon_y` | UI 图标位置 |
| `sidebar_x`、`sidebar_y`、`sidebar_free` | UI 面板位置与自由浮动 |
| `background_fade_seconds` | 背景切换渐变时长 |

`--no-save` 保留原配置不写回。

## 性能

2026-10-18 在真实 Wayland / KDE6 桌面重装当前构建，对比静态图片背景与动态壁纸。
本机为 Xeon E5-1680 v4 / RTX 3080 Ti，单屏 2560×1440、180 Hz，壁纸目标 60 FPS。
两轮采样均值相对前后干净静态基线的 **Plasma Shell 增量**：

| 后端 | 单逻辑 CPU 占用增量 | RSS 增量 | 进程显存增量 |
| --- | ---: | ---: | ---: |
| OpenGL | +17.7 个百分点 | +42.4 MiB | +69.0 MiB |
| Vulkan | +17.9 个百分点 | +84.1 MiB | +88.7 MiB |

CPU 的 100% 等于一个逻辑 CPU，以上增量约为本机 16 线程整机口径的 1.1%。
Vulkan 两轮 RSS 为 559–628 MiB，存在尚未定位的启动占用差异；切回静态后也有资源驻留。
KWin 开销、整卡功耗、逐秒数据和恢复检查见 [真实桌面使用报告](docs/plasma-desktop-2026-10-18.md)。
本次为短时测量，未测实际呈现帧率，不代表其他硬件或长期运行。

历史独立播放器测量（1920×1080、60 FPS、OpenGL，完整角色 + 背景 + UI）：

- 内存占用：约 140 MB
- 显存占用：约 60 MB
- 核心求值（参数推进、物理与几何构建）单帧 P50 约 0.45 ms，60 FPS 下约占 3% 单核；不含绘制、UI 和桌面合成

多后端对照、多角色连续切换的内存表现与详细测量方法见 [内存占用报告](docs/memory-footprint-2026-10-12.md) 与 [长期运行核查](docs/performance-audit-2026-10-16.md)。

## 诊断工具

- `nekomote_preview`：部件级预览与截图
- `nekomote_abi_host`：C ABI 隔离宿主，确定性回放与截图（`--assets`/`--model`/`--steps`/`--capture`/`--mute`）
- `nekomote_playback_probe`、`nekomote_session_probe`、`nekomote_frame_render`：逐帧数值与 GPU 帧观察

固定步截图须静音（`--mute --steps N --capture out.png`），独立于性能测量，不混入性能基准。

## 项目结构

```
src/model        强类型资产与数学基础
src/runtime      参数、场景图、动画、变形、物理求值
src/asset_io     资产加载与图片解码
src/abi          C ABI 壁纸实例（宿主接入边界）
src/render_gl    OpenGL 后端
src/render_vk    Vulkan 后端
src/application  会话、Controller、配置、多模型切换
src/ui           共享控制面板与布局
src/host_qt_quick Qt Quick 共享宿主基础
adapters/plasma  Plasma 6 壁纸 QML 模块
adapters/plasma5 Plasma 5 适配器（Qt5 插件注册与入口）
adapters/layer_shell 独立 Wayland layer-shell 壳
adapters/x11     独立 X11/EWMH 壳
apps/player      普通窗口播放器
apps/abi_host    C ABI 诊断宿主
apps/preview     部件预览
tools            导入、分发与验证脚本
```

模块边界、性能规范与开发约定见 [AGENTS.md](AGENTS.md)；开发路线图见 [ROADMAP.md](ROADMAP.md)；构建细节见 [docs/building.md](docs/building.md)。
