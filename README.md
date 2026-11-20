# NekoMote

<div align="center">
  <img src="./docs/branding/nekomote_logo.svg" alt="NekoMote" />
</div>

NekoMote 是专为 Linux 桌面动态壁纸打造的 E-mote 二维角色模型独立原生运行时（C++20）。

不同于传统的 WebGL/浏览器嵌入方案，NekoMote 的模型求值、时间线动画、4×4 双三次 Bézier 网格变形、遮罩合成、软弹簧物理模拟与 GPU 绘制均为**从零独立实现的完整原生播放链条**。它不依赖任何浏览器引擎、JavaScript、asm.js 或专有闭源库，能够在 Linux 桌面上以极低的 CPU、显存与功耗长期稳定运行。

目前本项目可无缝对接《猫娘乐园》（*NEKOPARA*）的解包素材（经 FreeMote 解包后由本项目离线导入器规范化），并支持通过原生 KDE Plasma 插件、独立 Wayland layer-shell 壳、独立 X11 壳或轻量独立播放器在各种 Linux 桌面环境中呈现。

<div align="center">
  <img width="960" height="540" alt="demo" src="https://github.com/user-attachments/assets/b3ccfd23-35d0-459f-b272-4cdc56304f28" />
</div>

效果演示视频：

https://github.com/user-attachments/assets/3df1f1a2-87f2-4c4a-a4eb-06ccd43b34a8

许可证：**GNU AGPL-3.0**（详见 [LICENSE](LICENSE)）。

---

## 核心特性

- **自研原生求值核心**：完整实现参数化、时间线、差分姿态叠加、网格变形、嵌套遮罩、软弹簧物理、视线追踪与触摸反应。
- **现代化双渲染后端**：原生支持 **OpenGL 3.3 Core** 与 **Vulkan 1.0** 双后端，启动运行前通过参数或配置指定，支持 PCI 硬件选卡（`--gpu pci:...`）。**不支持运行时热切换**，更换后端需重启壁纸进程或重新载入插件。
- **主流 Linux 桌面壁纸壳支持**：
  - **KDE Plasma 6 / 5 插件**：直接嵌入 Plasma Shell 场景图，提供原生图形化配置界面与多显示器管理。
  - **Wayland layer-shell 独立壳**：针对 Sway、Hyprland、niri、river 等现代 Wayland 合成器，支持全屏背景层常驻与全局视线追踪。
  - **GNOME Shell 45+（Wayland）**：通过 Shell 扩展将独立渲染进程接到桌面背景。[构建与使用](docs/gnome45.md)
  - **X11 / EWMH 独立壳**：针对 GNOME (X11)、XFCE、i3、bspwm、Openbox 等遵循 EWMH 的桌面环境与窗口管理器，以标准桌面根窗口常驻。
  - **独立播放器**：轻量 GLFW 窗口，支持开箱即用预览、单模型诊断与窗口挂件模式。
  - *(注：GNOME 仅在 Wayland 会话下受限，X11 会话已实测支持，见下文说明)*
- **开放自定义资产库**：资产库完全解耦，并不硬编码绑定特定作品。理论上任何使用 E-mote 引擎制作的 Galgame 角色动态立绘均可导入支持（详见资产说明）。
- **声音与拟真口型**：基于 PulseAudio 的低延迟音频播放，实时提取 RMS 能量驱动模型嘴部动作，支持一键静音。
- **基于时间的动态背景轮换**：支持静态 PNG 背景，以及根据本地系统时间平滑过渡的动态背景组。程序预留了通用的时间阶段配置接口，不仅限于三相，可自定义任意数量的时间相（如预设的白天/黄昏/夜晚）与过渡时长。
- **共享控制面板 UI**：内置后端无关的轻量 UI 面板，支持角色/背景实时切换、语言切换（简中/繁中/英/日）、FPS 显示、位置锁定与模型重置。
- **极佳性能与低功耗**：预热完成后稳态帧路径**零常态堆分配（Zero-Allocation）**，即使在十余年前的二代酷睿核显（i5-2410M / HD 3000）上也能以约 80 MiB 内存稳定达到 60 FPS。
- **规范的配置系统**：启动配置与面板偏好按 XDG 规范分离存储，资产目录保持严格只读，支持多实例独立配置。

---

## 壁纸壳支持与桌面适配

NekoMote 采用**应用层与壁纸壳完全解耦**的架构设计：核心业务逻辑、UI 绘制与状态管理统一由 C++ 共享库实现，并通过严格的 C ABI（0.8.0）暴露给各桌面适配器。

| 壁纸壳应用 | 适用桌面 / 合成器 | 图形后端 | 运行方式 | 交互与视线 | 配置机制 |
| --- | --- | --- | --- | --- | --- |
| **KDE Plasma 6 插件** | KDE Plasma 6 (Wayland / X11) | OpenGL / Vulkan (配置指定) | 嵌入 plasmashell 进程 | 支持点击触摸、拖拽与控制面板 | 桌面右键「配置壁纸」图形化设置 |
| **KDE Plasma 5 适配器** | KDE Plasma 5 (X11 / Wayland) | OpenGL / Vulkan (配置指定) | 嵌入 plasmashell 进程 | 支持点击触摸、拖拽与控制面板 | 桌面右键「配置壁纸」图形化设置 |
| **`nekomote_layer_shell`** | Sway, Hyprland, niri, river 等 | OpenGL / Vulkan (启动指定) | 独立后台进程 | 支持局部交互、鼠标穿透与全局视线 | 命令行参数 + XDG 独立配置文件 |
| **`nekomote_x11`** | GNOME (X11), XFCE, i3, bspwm, Openbox 等 | OpenGL / Vulkan (启动指定) | 独立后台进程 | 支持局部交互、鼠标穿透与全局视线 | 命令行参数 + XDG 独立配置文件 |
| **`nekomote_player`** | 任意桌面环境（窗口模式） | OpenGL / Vulkan (启动指定) | 独立前台窗口 | 完整键盘快捷键、鼠标拖拽与 UI 面板 | 命令行参数 + XDG 配置文件 |

> [!NOTE]
> **关于 GNOME 桌面环境的说明**：
> - **X11 会话（GNOME on X11）**：**完全支持**。GNOME 在 X11 下遵循标准 EWMH 规范，经实测可通过 `nekomote_x11` 稳定运行。
> - **Wayland 会话（GNOME on Wayland）**：暂不支持。原因在于 GNOME Shell (Mutter) 在 Wayland 下未实现 `wlr-layer-shell` 协议，且 GNOME 自身缺乏开放的外部 GPU 动态渲染壁纸接口。GNOME Wayland 用户若需体验，可切换至 GNOME X11 会话运行 `nekomote_x11`，或使用独立窗口播放器 `nekomote_player`。

---

## 壁纸使用指南

### 1. KDE Plasma 壁纸插件

GitHub Releases 提供预编译的分发包：
- Plasma 6：`nekomote-plasma6-debian12-<arch>.tar.gz`（`x86_64` / `aarch64`）
- Plasma 5：`nekomote-plasma5-ubuntu20-<arch>.tar.gz`（`x86_64` / `aarch64`）

解压后包含同名 `.wallpaper.zip`、SHA256 校验文件以及独立的交互式安装器 `installer.py`。

#### 安装与卸载

```bash
# 解压对应版本的安装包（以 Plasma 6 / x86_64 为例）
tar -xzf nekomote-plasma6-debian12-x86_64.tar.gz
cd nekomote-plasma6-debian12-x86_64

# 交互式管理菜单（支持 安装 / 重装 / 卸载 / 状态检查）
python3 installer.py

# 或使用非交互命令：
python3 installer.py install      # 安装壁纸包到 ~/.local/share/plasma/wallpapers/
python3 installer.py reinstall    # 同版本覆盖重装并清理旧模块
python3 installer.py uninstall    # 卸载插件（保留资产与用户偏好）
python3 installer.py status       # 检查当前安装状态与动态库依赖
```

*注：安装器需要 Python ≥ 3.8 以及系统自带的 `kpackagetool6`（或 `kpackagetool5`）。更新或安装后建议注销重新登录，以确保桌面载入最新动态库。*

#### 启用壁纸

1. 在桌面空白处点击右键 → 选择「配置桌面与壁纸（Configure Wallpaper）」。
2. 壁纸插件列表中选择 **NekoMote**。
3. 在设置页中指定你的 **资产目录（Assets Directory）**（例如 `~/Pictures/NekoMoteAssets`）。
4. （可选）在设置中选择渲染后端（OpenGL 或 Vulkan）、配置锁屏休眠自动暂停等。

---

### 2. Wayland layer-shell 独立壁纸 (`nekomote_layer_shell`)

面向支持 `wlr-layer-shell` 协议的 Wayland 合成器（Sway、Hyprland、niri 等），以 background 专有层常驻。

#### 快速启动

```bash
# 1. 列出当前检测到的显示器输出
nekomote_layer_shell --list-outputs

# 2. 默认 OpenGL 后端运行
nekomote_layer_shell --assets /path/to/assets --output DP-1

# 3. 指定 Vulkan 后端运行
nekomote_layer_shell --assets /path/to/assets --output DP-1 --backend vulkan
```

#### 多显示器配置

多显示器环境下，建议为每个屏幕启动独立的进程并显式绑定输出。次要屏幕建议添加 `--mute` 防止音频重叠：

```bash
# 主显示器：支持交互与声音
nekomote_layer_shell --config ~/.config/nekomote/main.json --assets /path/to/assets --output DP-1

# 副显示器：静音运行
nekomote_layer_shell --config ~/.config/nekomote/sub.json --assets /path/to/assets --output HDMI-A-1 --mute
```

#### 穿透与全局视线（Global Gaze）

- `--input interactive`（默认）：壁纸可见区域接收鼠标交互，可直接操作右上角 UI 面板或触摸角色。
- `--input passthrough`：鼠标事件完全穿透壁纸，不干扰桌面窗口；配合 `--global-gaze on`（默认开启）可实现**鼠标穿透点击桌面其他应用，但角色视线仍实时跟随光标移动**。

---

### 3. X11 / EWMH 独立壁纸 (`nekomote_x11`)

面向任意遵循 EWMH 规范的 X11 桌面环境与窗口管理器（包括 GNOME on X11、XFCE、i3、bspwm、Openbox、Awesome 等），以 `_NET_WM_WINDOW_TYPE_DESKTOP` 桌面层窗口常驻。

```bash
# 列出显示输出
nekomote_x11 --list-outputs

# 启动壁纸（默认 OpenGL）
nekomote_x11 --assets /path/to/assets --output HDMI-1

# 指定 Vulkan 后端与启动配置文件
nekomote_x11 --config ~/.config/nekomote/x11.json --assets /path/to/assets --output HDMI-1 --backend vulkan
```

*已知限制提示：在 XFCE 桌面下，若开启了桌面图标（`xfdesktop` 图标与背景为同一窗口），壁纸窗口可能会覆盖桌面图标。详见 [X11 壁纸壳使用说明](docs/x11.md)。*

---

### 4. 独立窗口播放器 (`nekomote_player`)

普通 GLFW 窗口，自动适配 Wayland 与 X11 会话，适合作为开箱即用测试工具、模型诊断或悬浮小挂件。

```bash
# 目录模式：加载完整资产，支持 UI 切换角色与背景
nekomote_player --assets assets/ --character chocola/date-a

# 单模型独立运行模式
nekomote_player --model assets/characters/chocola/date-a/model.nkm \
                --session assets/characters/chocola/date-a/session.nks.json --fit

# 无音频输出环境
nekomote_player --assets assets/ --mute
```

#### 键盘与鼠标快捷操作

| 操作 | 对应功能 |
| --- | --- |
| **鼠标移动** | 角色视线实时跟随 |
| **鼠标左键单击** | 触发角色触摸动作与语音 |
| **鼠标左键按住拖拽** | 移动角色在画面中的位置（长按或移动 >4px 触发） |
| **鼠标滚轮** | 围绕指针位置缩放角色大小 |
| **右上角齿轮图标** | 展开/折叠 UI 控制面板 |
| **空格键 (Space)** | 暂停 / 继续播放动画与物理 |
| **F 键** | 自动适配当前画幅大小（Fit） |
| **R 键** | 重置角色的缩放与位置 |
| **Tab / 数字键 1–9** | 快速切换可用角色 |
| **B / N 键** | 切换上一个 / 下一个背景 |
| **C 键** | 清除当前背景（透明底） |
| **Esc 键** | 退出播放器 |

---

### 5. 指定渲染 GPU 显卡

独立播放器、X11 壳和 layer-shell 壳均支持启动时通过 PCI 地址指定物理渲染设备：

```bash
# 1. 枚举系统可用显卡（无需显示服务器）
nekomote_player --list-gpus

# 2. 指定显卡运行（以 PCI 地址 0000:03:00.0 为例）
nekomote_player --assets assets/ --gpu pci:0000:03:00.0
nekomote_layer_shell --assets /path/to/assets --backend vulkan --gpu pci:0000:03:00.0
```

*注：明确指定 `--gpu` 后若设备不存在或不支持当前输出，程序将明确报错退出，绝不静默改选其它显卡。Plasma 插件自动沿用宿主合成器的 GPU。详见 [GPU 选择说明](docs/gpu-selection.md)。*

---

### 6. 配置持久化机制

NekoMote 采用**启动配置**与**面板运行时偏好**分离的双层设计，默认均存储在 XDG 用户配置目录（`~/.config/nekomote/`）下，**绝不写入资产目录**：
- **启动配置 (`.json`)**：记录资产目录路径、指定显示输出、渲染后端、GPU 地址、初始窗口尺寸等。通过 `--config FILE` 指定，只有带有 `--save-config` 参数时才会保存并退出。
- **面板偏好 (`.panel.json`)**：记录当前角色、当前背景、角色的缩放/平移偏移量、帧率限制（FPS）、是否静音、锁定状态、语言偏好等。**仅在用户通过界面操作时才会自动触发原子落盘**，首次启动或未更改设置时不会无故产生多余文件。

---

## 资产准备与导入

NekoMote 运行时只接受当前 **schema 6** 压缩二进制模型（`model.nkm`）与 **version 1** 交互会话（`session.nks.json`）。

> [!NOTE]
> **开放资产库与自定义构建说明**：
> NekoMote 的核心是一个通用的独立 E-mote 二维模型运行时，**资产库支持完全自定义构建，并不硬编码绑定《猫娘乐园》**。
> - **其他作品支持**：理论上任何采用 E-mote 引擎制作的 Galgame 游戏，只要能解包提取出其原始模型 JSON、`.resx.json` 资源映射与 PNG 贴图，并经本项目的离线导入工具（`nekomote_import` 及 `import_session.py`）转化为当前规范格式，都可以支持在 NekoMote 中加载与播放。
> - **实测范围边界**：需要特别说明的是，本项目目前的开发、几何精度回归测试与基准对齐均以《猫娘乐园》系列 195 个模型型号为基线，**尚未对其他 Galgame 作品的模型进行大范围实际测试与兼容性验证**。若其他作品使用了未经本项目实现验证的私有特性、未支持的继承位或特殊网格描述，可能会在导入或求值时被拒绝。欢迎社区开发者尝试导入并提交反馈与用例。

### 方式一：直接获取现成资产包（推荐）

你可以直接从配套的资产仓库 [NekoMoteAssets](https://github.com/Chocola-X/NekoMoteAssets) 下载已经导入并裁切优化好的现成资产包，解压后直接将其路径填入 `--assets` 或壁纸插件设置中即可。

### 方式二：从原游戏解包素材离线导入

1. **导入单个模型**：
   ```bash
   build/release/bin/nekomote_import model.json model.resx.json --output assets/characters/<角色组>/<型号>
   ```
2. **导入交互会话（反应规则、语音、触摸区域）**：
   ```bash
   python3 tools/import_session/import_session.py \
     --reference-root <原解包根目录> \
     --model <型号名> \
     --converter build/release/bin/nekomote_audio_convert \
     --character-root assets/characters/<角色组> \
     --output assets/characters/<角色组>/<型号>/session.nks.json
   ```
3. **批量导入全量资产与背景**：
   ```bash
   python3 tools/import_all_assets.py --reference-root <原解包根目录> \
     --importer build/release/bin/nekomote_import \
     --session-tool tools/import_session/import_session.py \
     --output assets/characters --report assets/import-report.json
   python3 tools/import_backgrounds.py --reference-root <原解包根目录> --dest assets
   ```

### 规范资产目录结构

```text
assets/
├── characters/
│   ├── characters.json               # 可选：角色显示名字典（支持 en / zh-CN / zh-TW / ja 多语言）
│   └── <角色组>/                     # 一级目录：角色组（如 chocola）
│       ├── audio/main/*.flac         # 会话语音（无损 FLAC，little/ 为小猫声线）
│       └── <型号变体>/               # 二级目录：具体型号（如 date-a）
│           ├── model.nkm             # schema 6 规范化模型（MessagePack + zlib 紧凑容器）
│           ├── session.nks.json      # 反应会话规则
│           ├── textures/             # 预裁切优化后的 PNG 图集
│           └── audio -> ../audio/main
├── backgrounds/
│   ├── background.json               # 可选：背景多语言显示名字典
│   ├── BG_07A.png                    # 静态背景图片（以文件名作为标识）
│   └── dynamic/
│       └── <组名>/                   # 动态背景组（如 BG_01）
│           ├── config.json           # 动态切换配置（自定义任意多 phases 与过渡时长）
│           ├── A.png                 # 阶段图片（文件名可自定义）
│           ├── B.png
│           └── C.png
└── icons/
    └── toggle.png                    # 面板图标
```

#### 动态背景的时间配置接口 (`config.json`)

程序内置了通用的基于时间的动态背景轮换引擎，不仅限于三相，可以在 `config.json` 的 `phases` 数组中自由定义任意数量的时间段：

```json
{
  "phases": [
    {"image": "A.png", "start": "06:00"},
    {"image": "B.png", "start": "12:00"},
    {"image": "C.png", "start": "16:00"},
    {"image": "D.png", "start": "18:00"}
  ],
  "transition_seconds": 1.8
}
```

- `start`：24 小时制 `HH:MM`。程序按当天分钟数排序，并根据本地系统时间自动切换到对应的阶段图片。
- `transition_seconds`：相与相之间平滑 crossfade 渐变的持续时间（秒）。

---

## 编译与安装

### 系统要求

- **构建工具**：CMake ≥ 3.25、Ninja、支持 C++20 的编译器（GCC ≥ 11 或 Clang ≥ 14）、Python 3 + Jinja2
- **核心依赖**：libpng 1.6、zlib、Fontconfig（文本渲染与多语言字体发现）、PulseAudio（libpulse ≥ 13.99）
- **显示与后端库**：
  - OpenGL 3.3 Core 驱动；Vulkan 1.0 支持需 Vulkan SDK（headers + `glslangValidator`）及 ICD loader
  - X11 壳需 `libX11`、`libXrandr`、`libXext` 开发包
  - Wayland layer-shell 壳需 `wayland-client ≥ 1.20`、`wayland-scanner`、`wayland-egl`、`wayland-cursor` 与 `EGL ≥ 1.5`
  - Plasma 插件需 Qt Quick（Plasma 6 需 Qt ≥ 6.4；Plasma 5 需 Qt ≥ 5.12）

### CMake 预设 (Presets) 汇总

| Preset | 类型 | 核心特性 | 包含组件 |
| --- | --- | --- | --- |
| `release` | Release | LTO 优化、OpenGL + Vulkan 1.0、警告即错误 | 独立播放器、所有导入/转换工具、ABI 诊断宿主 |
| `system` | Release | 完全使用发行版系统包（禁止任何自动联网下载） | 同 release |
| `dev` | Debug | 快速增量编译、调试符号、包含单元测试 | 全部工具与测试集 |
| `plasma` | Release | LTO、Qt6 + Plasma 6 QML 插件 | Plasma 6 壁纸包打包与安装产物 |
| `plasma5` | Release | LTO、Qt5 + Plasma 5 QML 插件 | Plasma 5 壁纸包打包与安装产物 |
| `layer-shell`| Release | LTO、轻量独立 Wayland 进程、无 Qt/GLFW 依赖 | `nekomote_layer_shell` 可执行程序 |
| `x11` | Release | LTO、轻量独立 X11 进程、无 Qt/GLFW 依赖 | `nekomote_x11` 可执行程序 |
| `asan` | Debug | 地址与未定义行为检测（ASan + UBSan） | 无头核心与 C ABI 测试 |

### 编译步骤

```bash
# 1. 编译全功能 Release 发布版（默认通过 FetchContent 拉取固定的轻量依赖）
cmake --preset release
cmake --build --preset release -j$(nproc)

# 2. 编译独立的 Wayland layer-shell 壁纸壳
cmake --preset layer-shell
cmake --build --preset layer-shell -j$(nproc)

# 3. 编译独立的 X11 壁纸壳
cmake --preset x11
cmake --build --preset x11 -j$(nproc)

# 4. 编译 KDE Plasma 6 插件包
cmake --preset plasma -DNEKOMOTE_BUILD_PLASMA_PROBE=OFF
cmake --build --preset plasma --target nekomote_plasma_stage

# 5. 发行版系统依赖模式（完全断网，要求系统预装依赖）
cmake --preset system
cmake --build --preset system -j$(nproc)
```

### 运行单元测试（无需显示服务器）

```bash
cmake --preset dev -DNEKOMOTE_BUILD_PREVIEW=OFF -DNEKOMOTE_BUILD_PLAYER=OFF
cmake --build --preset dev
ctest --test-dir build/dev -LE gpu --output-on-failure
```

---

## 性能测试与实测数据

NekoMote 在设计之初便将**超低资源消耗与能效**置于首位。

### 1. 低配实机：i5-2410M / Intel HD 3000 核显

2026-11-13 在一台具有典型代表性的老旧低配笔记本上进行了严格实测：
- **硬件配置**：Intel Core i5-2410M（双核四线程，2.3 GHz）、Intel HD 3000 核显、3.7 GiB 内存
- **系统环境**：Debian 12、Mesa 22.3.6、OpenGL 3.3 Core、无头 X11 会话
- **测试条件**：1920×1080 分辨率、目标 60 FPS、完整模型动态求值、预热 120 帧后连续采样 600 帧（三轮中位数）：

| 测试角色型号 | 实测帧循环节奏 | 播放器进程 CPU（整机四线程口径） | 播放器物理内存峰值 (RSS) |
| --- | ---: | ---: | ---: |
| 巧克力 `chocola/date-a` | **59.05 FPS** | **6.43%**（相当于单核 ~25.7%） | **79.66 MiB** |
| 巧克力 `chocola/wintermaid` | **58.98 FPS** | **7.62%**（相当于单核 ~30.5%） | **79.32 MiB** |
| 椰子 `coconut/casual` | **58.89 FPS** | **6.49%**（相当于单核 ~26.0%） | **79.70 MiB** |

在 1080p / 30 FPS 的工况下，播放器 CPU 占用进一步下降至全机 **3.5%–3.9%**。经独立 CPU 探针测量，单帧核心求值与构帧耗时（P50）仅为 **0.44–0.79 ms**，且预热完成后受测路径的常规 C++ 堆内存分配计数严格为零。测试详情见 [低配实机测试报告](docs/headless-lowend-test-2026-11-13.md)。

### 2. 真实 Wayland 桌面：Plasma 6 插件与 layer-shell

2026-11-20 使用 Debian 12 CI 产物，在 Xeon E5-1680 v4 / RTX 3080 Ti 的 Plasma 6.3.6 Wayland 桌面上测试同一角色，分辨率 2560×1440 @ 180 Hz，壁纸限帧 60 FPS。下表为两轮均值相对静态壁纸的增量。

| 入口 | 桌面 CPU 增量（单逻辑 CPU = 100%） | 整卡显存增量 | 板卡功耗增量 |
| --- | ---: | ---: | ---: |
| Plasma OpenGL | +28.0 个百分点 | +69.0 MiB | +10.3 W |
| Plasma Vulkan | +27.7 个百分点 | +92.5 MiB | +11.6 W |
| layer-shell OpenGL | +18.5 个百分点 | +164.4 MiB | +6.5 W |
| layer-shell Vulkan | +14.6 个百分点 | +123.5 MiB | +6.3 W |

CPU 统计包含 Plasma Shell、KWin 以及运行中的独立壳。Plasma OpenGL 的 Plasma Shell RSS 增加约 35 MiB。逐轮数据和帧率口径见 [桌面实测报告](docs/wayland-desktop-performance-2026-11-20.md)。

### 3. 真实 X11 桌面：Plasma 6 插件与独立 X11 壳

同一台机器切换到原生 X11 后，沿用同一角色、2560×1440 @ 180 Hz 和 60 FPS 上限。下表为两轮均值相对静态壁纸的增量，桌面 CPU 包含 Plasma Shell、KWin、Xorg 及运行中的独立壳。

| 入口 | 桌面 CPU 增量（单逻辑 CPU = 100%） | 整卡显存增量 | 板卡功耗增量 |
| --- | ---: | ---: | ---: |
| Plasma OpenGL | +21.1 个百分点 | +87.8 MiB | +8.1 W |
| Plasma Vulkan | +20.5 个百分点 | +115.3 MiB | +11.8 W |
| X11 壳 OpenGL | +19.4 个百分点 | +186.1 MiB | +6.2 W |
| X11 壳 Vulkan | +18.7 个百分点 | +180.4 MiB | +10.1 W |

Plasma 在 X11 下约 58.8 次/秒壁纸绘制和窗口交换，独立壳约 57.7–58.4 次/秒提交。两轮静态基线的整卡显存相差约 65 MiB，逐轮波动和进程归属见 [X11 桌面实测报告](docs/x11-desktop-performance-2026-11-20.md)。

---

## 项目代码结构

```text
src/model/            强类型不可变模型资产定义、几何与数学基础
src/runtime/          参数化、场景图、时间线动画、网格变形、软弹簧物理求值
src/asset_io/         紧凑格式加载、资产校验、图集与 PNG 硬件解码
src/render_gl/        OpenGL 3.3 Core 渲染后端执行器与 GPU 状态管理
src/render_vk/        Vulkan 1.0 渲染后端执行器与设备命令录制
src/application/      壁纸全局 Controller、会话调度、配置持久化、音频与交互状态机
src/ui/               后端无关轻量 UI 面板布局、Hit-Test、多语言与绘制协议
src/abi/              面向外部桌面宿主的高层稳定 C ABI (0.8.0)
src/host_qt_quick/    Qt Quick / Plasma 宿主共享胶水层
src/host_vulkan/      独立的平台 Vulkan WSI 交换链呈现辅助
adapters/plasma/      KDE Plasma 6 官方壁纸 QML 插件
adapters/plasma5/     KDE Plasma 5 适配器插件
adapters/layer_shell/ 针对现代 Wayland 合成器的独立 layer-shell 壁纸进程
adapters/x11/         针对 X11/EWMH 窗口管理器的独立桌面壁纸进程
apps/player/          基于 GLFW 的通用交互播放器
apps/abi_host/        纯 C ABI 确定性诊断与自动化回归验证宿主
apps/preview/         部件级网格与图集诊断预览工具
tools/                模型导入、音频转换、PNG 优化与自动化打包脚本
docs/                 架构规范、各模块契约、使用说明与详细实测报告
```

---

## 文档导航

- **架构与编码约定**：[AGENTS.md](AGENTS.md)
- **开发演进路线图**：[ROADMAP.md](ROADMAP.md)
- **C ABI 契约规范**：[docs/abi-contract.md](docs/abi-contract.md)
- **配置系统指南**：[docs/configuration.md](docs/configuration.md)
- **GPU 选卡指南**：[docs/gpu-selection.md](docs/gpu-selection.md)
- **编译与构建指南**：[docs/building.md](docs/building.md)
- **依赖说明与系统打包**：[docs/dependencies.md](docs/dependencies.md)
- **KDE Plasma 分发与安装**：[docs/plasma-distribution.md](docs/plasma-distribution.md)
- **Wayland layer-shell 使用说明**：[docs/layer-shell.md](docs/layer-shell.md)
- **X11 壁纸壳使用说明**：[docs/x11.md](docs/x11.md)
- **文档完整导航索引**：[docs/README.md](docs/README.md)
