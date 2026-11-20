# GNOME Shell 45+ 壁纸适配器

`adapters/gnome45/` 提供 GNOME Shell 扩展和独立渲染进程 `nekomote_gnome45`。
扩展通过 Mutter 的 `Meta.WaylandClient` 启动和识别自己的窗口，在 Shell 背景组放置其
`Clutter.Clone`。渲染进程使用 xdg-shell、现有 Wayland EGL/Vulkan 宿主和共享壁纸 C ABI。
关闭扩展、退出会话或重建输出时，扩展撤下克隆并终止对应进程。GNOME 原有背景随即恢复可见。

## 构建与安装

用 CMake 3.25+、C++20、Ninja、GLib 的 `glib-compile-schemas` 以及
[Wayland 宿主的构建依赖](layer-shell.md#构建与安装)编译。默认同时启用 OpenGL 与 Vulkan。

```bash
cmake --preset gnome45
cmake --build --preset gnome45
cmake --install build/gnome45 --prefix ~/.local --component gnome45
```

只构建 OpenGL 时，配置阶段追加 `-DNEKOMOTE_BUILD_VULKAN=OFF`，扩展中选择 OpenGL。
扩展目录为 `~/.local/share/gnome-shell/extensions/nekomote@nekopara.uk/`，
渲染程序安装到 `~/.local/bin/`。首次安装后若 Shell 未发现扩展，重新登录 GNOME Wayland。

```bash
gnome-extensions enable nekomote@nekopara.uk
gnome-extensions prefs nekomote@nekopara.uk
```

在偏好页填入含 `characters/` 的资产根目录绝对路径，并应用。默认使用 OpenGL。
如果渲染程序放在其他位置，可填入其绝对路径。清空资产目录或禁用扩展即可恢复静态背景。
启动失败可在用户会话的 GNOME Shell 日志中查找 `NekoMote`。

## 当前行为

- 扩展按逻辑显示器启动独立实例。只有主显示器实例启用声音，面板状态保存在
  `$XDG_CONFIG_HOME/nekomote/gnome45/monitor-N.panel.json`（默认配置根为 `~/.config`）。
  状态按 GNOME 的显示器索引区分。可按 [面板状态格式](configuration.md) 预先选择角色、
  背景和帧率。
- 首版桌面背景让鼠标事件透传。共用的壁纸面板、触摸、拖拽和全局视线暂未接入 GNOME
  背景 actor。这个接入面向 GNOME Wayland，GNOME X11 使用独立 X11 壳。
- 扩展只在普通用户会话模式启动渲染器。宿主沿用已有会话暂停策略。
- 源码参考了 Hanabi 的独立客户端与背景克隆思路，以及 Wallpaper Engine for Wayland
  的背景层与窗口所有权设计。适配器独立实现，不引入这两个参考项目作为依赖。

## 隔离验收

2026-11-20 使用 `experiments/gnome-adapter-2026-11-20/` 下两个私有根文件系统。
Ubuntu 23.10 提供 GNOME Shell 45.2，Debian 13 提供 GNOME Shell 48.7。
运行时隔离 HOME、XDG 目录、进程、网络、Xvfb、Wayland socket、会话总线与测试系统总线。
只读挂载真实素材，使用 Mesa llvmpipe / lavapipe。宿主工具链生成的渲染程序在
GNOME 45 测试中使用隔离目录中的 C++ 运行库，GNOME Shell 本身使用发行版原装库。

两个版本均在单个 800×600 逻辑输出下验证 `chocola/wintermaid-b` 和 `BG_07A`。
OpenGL 和 Vulkan 都有间隔截图证明桌面角色在更新。检查了渲染窗口最小化并从任务列表排除、
克隆属于桌面背景组、切换后端、禁用后恢复静态壁纸、重新启用、打开偏好页和停掉渲染进程。
Release/LTO/Werror 构建通过。共用宿主的 layer-shell 既有三组测试通过，私有 KWin
OpenGL 和 Vulkan 集成各 17 项通过。

扩展元数据列出 GNOME 45–50。实际运行验证覆盖 45.2 和 48.7，46、47、49 和 50 未实测。
49 起使用 Mutter 的新子进程接口，旧的任务列表隐藏方法不可用，任务切换器中的行为有待复核。
本次未验收物理多屏、分数缩放、锁屏恢复和硬件 GPU 性能。

隔离启动脚本、日志和截图保存在本地实验目录。最终截图分别位于
`mantic-final/` 和 `trixie-final/`。测试只在私有根文件系统中启用扩展。
