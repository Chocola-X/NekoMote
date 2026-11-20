# GNOME Shell 45+ 壁纸适配器

`adapters/gnome45/` 提供 GNOME Shell 扩展和独立渲染进程 `nekomote_gnome45`。
扩展通过 Mutter 的 `Meta.WaylandClient` 启动和识别自己的窗口。桌面与工作区预览通过
`Clutter.Clone` 显示同一个渲染画面。渲染进程使用 xdg-shell、现有 Wayland EGL/Vulkan 宿主和
共享壁纸 C ABI。
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
- 在桌面上点击右上角的按钮可打开共享控制面板。角色支持左键触摸、拖拽、滚轮缩放和桌面
  范围内的鼠标视线。右键仍打开 GNOME 的桌面菜单。顶部面板占用的空间会留给 GNOME，
  打开活动概览或鼠标离开壁纸时结束当前壁纸手势。
- 活动概览和工作区预览以原比例居中填充 GNOME 的圆角工作区框，超出框的部分会裁切。
  圆角效果只应用于预览。壁纸交互限于裸露的桌面区域，窗口覆盖下的全局视线暂未接入。
  GNOME X11 使用独立 X11 壳。
- Shell 通过渲染子进程的专用标准输入管道异步转发鼠标事件。输入以实际显示壁纸的克隆为
  原点，由宿主一次性转换为输出物理像素。概览复用桌面渲染实例，每台显示器只运行一个
  渲染进程。
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

交互复测在两个版本中调用扩展的输入回调，并单独检查 Clutter 在控制按钮位置拾取到
桌面背景 actor。OpenGL 和 Vulkan 均可打开共享面板。OpenGL 下验证了滚轮缩放和拖拽
位移保存，右键事件继续由 Shell 分发。静置桌面在两个后端都持续更新，活动概览中的
OpenGL 预览也有间隔截图确认。输入测试覆盖 Shell 回调到共享壁纸的链路，实体鼠标的
完整事件分发留待手动验收。

随后在两个版本的 1920×1080 会话复测几何。将 GNOME 原生输入背景偏移 32 逻辑像素
模拟顶栏高度后，旧坐标错过控制按钮，按渲染克隆坐标点击可命中。OpenGL 和 Vulkan 的
概览截图均与 GNOME 原生框对照确认圆角和外边界对齐，预览内部保留源画面的 16:9 比例，
桌面和概览中的动画也持续更新。几何复测截图位于
`mantic-aspect-final/` 和 `trixie-aspect-final/`。

冷启动复测在 Shell 启动时自动启用扩展。旧版在活动概览中启动渲染器后，原窗口可能在
标记为最小化的情况下仍覆盖壁纸克隆，实测画面向下偏移 32 像素。仅将原窗口设为透明时，
其冷启动状态仍可能参与 Mutter 的局部绘制裁切。扩展现在隐藏原窗口 actor，桌面和概览
由克隆呈现。

在 GNOME 45.2 和 48.7 中验证了冷启动及重新启用后的拖拽位置和重绘。在 GNOME 48.7
的私有 Xephyr 会话中，旧版同样复现了冷启动时原窗口仍被映射的状态差异。两版的
自动拖拽和局部重绘截图均未复现实体鼠标测试报告的色块。截图及诊断数据位于本地实验目录的
`*-damage-*`。Vulkan 通过 Wayland 日志检查概览中的帧回调。

扩展元数据列出 GNOME 45–50。实际运行验证覆盖 45.2 和 48.7，46、47、49 和 50 未实测。
49 起使用 Mutter 的新子进程接口，旧的任务列表隐藏方法不可用，任务切换器中的行为有待复核。
本次未验收物理多屏、分数缩放、锁屏恢复和硬件 GPU 性能。

隔离启动脚本、日志和截图保存在本地实验目录。交互复测截图分别位于
`mantic-desktop-input/` 和 `trixie-desktop-input/`。
在已配置的本地实验环境中，从仓库根目录运行下列命令可打开 1920×1080 的隔离窗口。
将 `48` 替换成 `45` 可切换 Shell 版本，末尾可指定 `opengl` 或 `vulkan`。

```bash
bash experiments/gnome-adapter-2026-11-20/launch-visible.sh wayland 48
```
