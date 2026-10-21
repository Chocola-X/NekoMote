# Wayland layer-shell 适配器

2026-10-21 双后端交付与限定验收见 [报告](shell-vulkan-2026-10-21.md)。

## 实现与边界

`adapters/layer_shell/` 提供独立程序 `nekomote_layer_shell`。它只通过高层
`nekomote_wallpaper_*` C ABI 使用已有 application、UI、音频和 OpenGL/Vulkan 渲染。
不增加模型求值公式或重写渲染引擎。初版新增的
`nekomote_wallpaper_set_config_path` 用于让多个输出独立保存配置；Vulkan
呈现设备和交换链复用独立的共享宿主工具。

宿主创建 `zwlr_layer_shell_v1` 的 background surface，四边锚定，覆盖完整输出，
不占用其他窗口的工作区域，不申请键盘焦点。一个进程对应一个输出；合成器指定的
配置尺寸、整数缩放及本地指针坐标在壳内转换为 ABI 的物理像素。

目标是提供 wlr-layer-shell 的 Wayland 合成器，包括 Sway、Hyprland、niri 等。
协议通用不等于这些合成器均已验收。GNOME、X11 不在此入口覆盖范围；KDE 继续优先
使用现有 Plasma 插件，隔离 KWin 仅作为当前可用的协议验证环境。

## 构建与安装

在项目通用构建依赖之外，需要：

- `pkg-config`、`wayland-scanner`、Wayland client ≥ 1.20。
- `wayland-egl`、`wayland-cursor` 开发文件与 EGL ≥ 1.5。
- OpenGL 开发头文件和可创建 desktop OpenGL 3.3 core 上下文的 EGL 驱动。
- 启用 Vulkan 时需要 Vulkan 开发文件与支持 Wayland WSI 的 Vulkan 驱动。
- 运行环境提供默认光标主题（interactive 模式）。

Debian 12 / Ubuntu 22.04 及更新系统的相关开发包通常为
`libwayland-dev libwayland-bin libegl1-mesa-dev libgl-dev`。
Ubuntu 20.04 原版 Wayland 1.18 不满足本适配器的构建下限；已有 Ubuntu20 本体和
Plasma5 CI 配方不启用此目标，不受影响。

```bash
cmake --preset layer-shell
cmake --build --preset layer-shell -j 4
```

该 preset 启用 Release、LTO、Werror、C ABI 和 Vulkan，关闭 GLFW、Qt 与 Plasma。
默认仍选择 OpenGL；`--backend vulkan` 显式选择 Vulkan。可以用
`-DNEKOMOTE_BUILD_VULKAN=OFF` 构建仅 OpenGL 的程序，此时 Vulkan 参数明确报错。
协议 XML 固定在源码中，来源、许可和本地修改见
[协议说明](../adapters/layer_shell/protocols/README.md)，构建不另行下载协议。

只安装本适配器（按需要选择可写前缀）：

```bash
cmake --install build/layer-shell --prefix "$HOME/.local" --component layer_shell
```

GitHub Actions 新增 Debian 12 / GCC12 / Wayland1.21 基线构建，提供
`nekomote-layer-shell-debian12-x86_64.tar.gz` 与
`nekomote-layer-shell-debian12-aarch64.tar.gz`，并纳入版本标签发布。
解压后执行 `nekomote-layer-shell/bin/nekomote_layer_shell`；包中不附带 README、
模型或系统库，项目 AGPL-v3 许可证为 `LICENSE`，依赖及协议声明为 `THIRD_PARTY_NOTICES`。
运行时仍需系统的 Wayland、EGL、OpenGL、Vulkan loader、PulseAudio、PNG/zlib 与 Fontconfig。
基线符号审计和本地验证范围见 [CI 说明](github-actions.md#layer-shell-分发与归档精简2026-10-18)。

## 使用

在目标 Wayland 会话中执行，素材目录沿用现有已导入资产：

```bash
build/layer-shell/bin/nekomote_layer_shell --list-outputs
build/layer-shell/bin/nekomote_layer_shell --assets /path/to/assets --output DP-1
build/layer-shell/bin/nekomote_layer_shell --assets /path/to/assets --backend vulkan
```

不指定 `--output` 时由合成器选择输出。显式名称依赖 `wl_output` v4；旧合成器只能使用
默认选择。多屏分别启动进程，并显式选择不同输出；建议次要屏幕使用 `--mute`，避免
各实例同时发声。不要同时让其他壁纸程序占用同一个 background 输出。

| 参数 | 含义 |
| --- | --- |
| `--assets DIR` | 正式资产目录，共用角色/背景扫描、切换及控制面板 |
| `--output NAME` | 固定输出，移除该输出后正常退出 |
| `--config FILE` | 启动配置文件，显式参数覆盖已保存值 |
| `--state FILE` | 面板偏好文件，默认由启动文件路径派生 |
| `--save-config` | 显式保存启动设置并退出，无需显示服务器 |
| `--backend opengl\|vulkan` | 图形后端，默认 OpenGL；Vulkan 需要构建时启用 |
| `--input interactive` | 默认；背景可见处接收本地指针，转发共享 UI 和模型交互 |
| `--input passthrough` | 空输入区域；全部穿透，不支持鼠标控制和视线交互 |
| `--mute` | 强制静音 |
| `--no-save` | 不保存配置 |
| `--ui-open` | 资产加载后展开共享控制面板，便于诊断 |
| `--model FILE [--session FILE]` | 显式模型诊断入口，不绘制资产控制面板 |

interactive 接收背景上的指针事件，不保证桌面的右键菜单或图标层行为；layer-shell
没有通用的逐按钮穿透机制。本程序不采集其他窗口上的全局鼠标位置。窗口上方是否有
桌面图标、背景右键菜单如何实现，由目标桌面决定。

默认启动配置为 `$XDG_CONFIG_HOME/nekomote/layer-shell.json`，默认面板偏好为
同目录的 `layer-shell.panel.json`；XDG 未设置或不是绝对路径时使用 `$HOME/.config/`。
多个实例分别用 `--config` 指定独立启动文件，面板文件由其路径派生，也可用 `--state`
指定。输出名称不再参与配置标识。没有默认资产目录，启动参数仅在 `--save-config`
时持久化；面板只保存用户操作数据。旧配置迁移与完整规则见[配置说明](configuration.md)。

配置路径由壳选择，schema、默认值、读写时机和模型状态仍由共享应用负责。新增 ABI
在首次资产设置开始后锁定路径；显式路径不可用时报告错误，不回退到其他位置。
普通读取和 `--no-save` 不创建配置目录，但允许读取已有可读配置。
没有可用的 seat v5 指针能力时报告 unavailable，基础动画继续运行；不能仅由
interactive 模式启动成功推定实际鼠标或光标已验收。

## 调度与生命周期

Wayland 连接、图形设备、输入队列和壁纸实例由同一个线程拥有。采用
`wl_surface.frame` 与共享 FPS 限制共同调度，等待事件时使用 poll；没有常态截图、
GPU 读回或忙循环。帧回调暂停期间不推进求值，恢复时传入的 delta 最大 250ms。
角色渐变沿用共享 application 的同一推进预算，不恢复墙钟渐变。

配置变化触发尺寸/缩放更新；layer close 或目标输出移除结束进程，合成器连接断开
报告失败，SIGINT/SIGTERM 正常退出。Vulkan 路径先等待 GPU 完成，再销毁壁纸实例
及工作线程，之后释放图形设备、surface 和 Wayland 连接。启动连接中的同步
roundtrip 没有独立内部超时。

OpenGL 路径使用 EGL/desktop GL；Vulkan 路径在同一 layer surface 上创建原生
`VkWaylandSurfaceKHR`，不创建 EGL 上下文或 `wl_egl_window`。共享
`nekomote::host_vulkan` 管理设备、交换链和单帧提交，壳保持 configure/ack、
buffer scale 和 frame callback 节奏。每次修改共享实例（包括输入、resize、advance）前
等待上一帧 GPU 工作完成，帧回调在实际 present 前申请；正式帧不读回。
整数缩放受支持；尚未实现
fractional-scale/viewporter，分数缩放效果由合成器重采样。没有通用遮挡检测或显式
锁屏通知；依赖合成器是否抑制不可见 surface 的帧回调，不能保证所有桌面都自动暂停。

## 验证入口

```bash
cmake --preset layer-shell -DNEKOMOTE_BUILD_TESTS=ON
cmake --build --preset layer-shell -j 4
ctest --test-dir build/layer-shell -R 'layer_shell_options|abi_synthetic_boundary' --output-on-failure
```

启用测试额外需要 `wayland-server` 开发文件。无 layer-shell 能力的负例服务器只连接
私有 runtime；完整虚拟 KWin 验证及证据格式见
[测试说明](../tests/layer_shell/README.md)。不安装其他桌面、不连接当前桌面 socket、
不注入系统键鼠。

`tools/testing/run_layer_shell_test.py --output-tool /usr/bin/kscreen-doctor` 可额外检查
运行中的整数缩放：只在脚本自建的私有 KWin、D-Bus 与 Wayland socket 中，将选定
输出从 scale1 改为 scale2，检查客户端收到 scale 事件、提交 buffer scale2、材质画面
及正常退出，并在 finally 恢复 scale1。未提供工具时结果明确记录未执行。
缩放通常保持物理模式尺寸不变，此项不作为运行中物理尺寸/交换链重建或热插拔验收。

诊断示例（截图文件必须不存在）：

```bash
build/layer-shell/bin/nekomote_layer_shell --model /path/model.nkm \
    --session /path/session.nks.json --frames 4 --capture /tmp/nekomote-frame.ppm
```

`--frames` 固定随机种子和 1/60 秒步长，强制静音与不保存；`--capture` 仅在该模式
最后一帧执行一次诊断读回。PPM 统一为左上原点 RGB，Vulkan 的 straight RGBA
按黑底合成，保持与 OpenGL RGB 读回相同的输出约定。不同初始尺寸的重启不代替
运行中 resize/热插拔测试。Vulkan 可用 `NEKOMOTE_VK_VALIDATION=1` 显式启用
Khronos validation layer；缺少该层时明确失败，不作为普通运行依赖。

2026-10-21 新增 Vulkan 接入，下面 L1 证据只证明当时的 OpenGL 版本。
本次双后端验收结果由集成报告单独记录，不扩展为目标合成器实机或长期认证。

## 本轮结果（2026-10-15，L1）

基线 `98d0075`，Codex 实现并进行独立源码复核。GCC 15.2.1、Wayland 1.24.0、
KWin 6.3.6；正式 preset 的 Release/LTO/Werror 构建与隔离前缀安装通过。
使用固定依赖的本地缓存，不代表首次联网配置或 Ubuntu20 基线构建已执行。

| 验证 | 结果 |
| --- | --- |
| Release/Werror 无窗口 CTest，排除 integration/gpu | 192/192，通过；Python 子套件另有 12 项 skipped，未计为执行通过 |
| CLI 契约 | 5 个用例 / 44 条断言（包含在上述 CTest 中） |
| ABI 合成边界，含配置路径新增测试 | 7 个用例 / 88 条断言（包含在上述 CTest 中） |
| Release/LTO 虚拟 KWin 协议及客户端 framebuffer | 18/18，通过，Mesa 25.2.3 软件 GL + KWin QPainter |
| 真实资产加载 | 1280×720、固定四帧、强制静音/no-save，客户端截图已目视检查；非全模型动态验收 |

其中独立核心套件为 188 个用例 / 38,765 条断言；Python 子套件执行入口报告 129 项，
117 通过 / 12 因未启用可选宿主等条件跳过。上述数量属于不同 runner，不相加为同一种测试数量。

18 项覆盖能力缺失拒绝、双输出枚举/选择、interactive/passthrough 启动、重复确定画面、
正式资产入口、初始尺寸 320×240 / 400×300 / 1280×720、真实 buffer scale2、截图防覆盖、
连接断开失败退出、SIGTERM 正常退出及不保存配置。原始证据位于
`build/evidence/layer-shell-2026-10-15-final2/`，可提交的摘要见
[验证记录](validation/layer-shell-results.json)。客户端二进制 SHA256：
`1ed957537d420ce095c76292c0dc32e2cc5ebe9b7baeabcff1825de34a6232e2`。

首轮失败与修正：合成 tile 需要抵消共享应用默认的角色原点；这是测试输入修正。
KWin 的 `--scale 2` 单独只放大虚拟模式，本轮另配置私有输出文件，并断言实际 scale2。
NVIDIA 580.82.09 + 虚拟 OpenGL 合成器出现 EGLImage/texture-target 不兼容错误，
即使客户端截图正确也不能判最终画面通过；Mesa/QPainter 隔离轮没有该错误，但未取得
合成器输出像素。Spectacle 在此虚拟环境拒绝截图，最终合成画面仍明确未验收。虚拟会话无可用 seat v5 指针，interactive 启动测试没有覆盖真实指针回调或光标 buffer 提交。

源码复核发现并修复 pending 事件分发后的旧超时，以及主题光标宽高不整除 buffer scale
可能导致的协议错误。旧 ABI 分配探针拆到独立 TU，避免 GCC15 优化构建的
`mismatched-new-delete` 假阳性；没有禁用警告或删减 OOM/零分配断言。

未覆盖：Sway/Hyprland/niri 实机、最终合成画面、物理输出、真实指针交互、运行中
resize/热插拔、音频播放、多进程同时输出、完整动画、长期与功耗。没有测量性能收益，
不能将软件渲染验证当作实际壁纸的资源消耗证据。
