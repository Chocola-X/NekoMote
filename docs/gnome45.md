# GNOME Shell 45+ 壁纸适配器

`adapters/gnome45/` 提供 GNOME Shell 扩展和独立渲染进程 `nekomote_gnome45`。
扩展通过 Mutter 的 `Meta.WaylandClient` 启动和识别自己的窗口。桌面与工作区预览通过
`Clutter.Clone` 显示同一个渲染画面。渲染进程使用 xdg-shell、现有 Wayland EGL/Vulkan 宿主和
共享壁纸 C ABI。
关闭扩展、退出会话或重建输出时，扩展撤下克隆并终止对应进程。GNOME 原有背景随即恢复可见。

## 构建与安装

版本发布提供 `nekomote-gnome45-ubuntu22-x86_64.tar.gz` 和
`nekomote-gnome45-ubuntu22-aarch64.tar.gz`。Ubuntu 22.04 是原生程序的构建基线，
运行扩展需要 GNOME Shell 45 或更新版本的 Wayland 会话。
解压后在包内运行 `bash install.sh` 可安装扩展和渲染程序。升级时禁用扩展后再运行同一命令，
并重新登录 GNOME。运行 `bash install.sh uninstall` 可移除这两个组件。

### 从源码构建

用 CMake 3.25+、C++20、Ninja、GLib 的 `glib-compile-schemas`、gettext 的 `msgfmt` 以及
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

在偏好页选择含 `characters/` 的资产根目录。资产目录和渲染程序路径均可浏览选择或手动输入。
偏好页随 GNOME 会话语言显示，当前提供英文和简体中文。
启用扩展时会填入找到的默认程序路径，优先使用 `~/.local/bin/nekomote_gnome45`，
其次搜索 `PATH`。自定义渲染程序需与扩展来自同一构建。默认使用 OpenGL。
清空资产目录或禁用扩展即可恢复静态背景。
启动失败可在用户会话的 GNOME Shell 日志中查找 `NekoMote`。

需要保留日志时，在偏好页的「诊断」中打开「保存诊断日志」（默认关闭），即可看到「日志保存目录」。
可浏览选择目录，或手动输入绝对路径并应用。默认目录为
`$XDG_STATE_HOME/nekomote/gnome45/diagnostics`（`XDG_STATE_HOME` 未设置时使用 `~/.local/state`）。
清空目录并应用可恢复默认值。

开启日志或修改保存目录会重启壁纸渲染器。每次启动记录到一个新的 `session-*` 子目录，其中
`extension.log` 记录扩展事件，各显示器的 `monitor-N.log` 保存渲染程序的标准输出和错误输出。
排查时请提供复现所对应的整个子目录。在 Live 会话中，可以直接选择已挂载的持久化磁盘，
或在重启前把日志复制到该磁盘。

如果日志显示无法读取资产目录下的 `characters/`，请在当前 GNOME 会话确认磁盘已挂载，
且扩展配置的路径与实际挂载路径一致。日志会记录失败路径和系统返回的错误。

如果扩展已启用却仍显示静态壁纸，在出现现象的 GNOME 会话中检查：

```bash
gnome-shell --version
gnome-extensions info nekomote@nekopara.uk
journalctl --user -b --no-pager -g 'NekoMote|nekomote_gnome45|GNOME (OpenGL|Vulkan)'
```

扩展日志会分别记录渲染进程启动、接入桌面及进程退出。进程启动后未接入桌面时，
可暂时关闭偏好页中的全屏暂停，再退出活动概览测试。也可确认渲染后端为 OpenGL，
并检查扩展及渲染程序是否来自同一份安装包。

## 当前行为

- 扩展按逻辑显示器启动独立实例。只有主显示器实例启用声音，面板状态保存在
  `$XDG_CONFIG_HOME/nekomote/gnome45/monitor-N.panel.json`（默认配置根为 `~/.config`）。
  状态按 GNOME 的显示器索引区分。可按 [面板状态格式](configuration.md) 预先选择角色、
  背景和帧率。
- 在桌面上点击右上角的按钮可打开共享控制面板。角色支持左键触摸、拖拽、滚轮缩放和桌面
  范围内的鼠标视线。右键仍打开 GNOME 的桌面菜单。顶部面板占用的空间会留给 GNOME，
  打开活动概览或鼠标离开壁纸时结束当前壁纸手势。
- 活动概览和工作区预览以原比例居中填充 GNOME 的圆角工作区框，超出框的部分会裁切。
  圆角效果只应用于预览。点击、拖拽和滚轮限于裸露的桌面区域。默认允许视线跟随桌面外及
  其他显示器上的指针，可在偏好页关闭。
- 渲染窗口由扩展保持最小化，并从应用和窗口切换列表中隐藏。桌面和活动概览显示的是
  扩展接入的壁纸画面。
- 活动概览顶部的工作区缩略图也使用同一个动态壁纸画面，普通窗口仍显示在壁纸上方。
  使用 Manjaro 自带的 GNOME UI Tune 时，停用 NekoMote 会恢复其缩略图静态背景。
  GNOME X11 使用独立 X11 壳。
- Shell 通过渲染子进程的专用标准输入管道异步转发鼠标事件。输入以实际显示壁纸的克隆为
  原点，由宿主一次性转换为输出物理像素。概览复用桌面渲染实例，每台显示器只运行一个
  渲染进程。
- 默认根据宿主会话事件暂停播放，也会在显示器进入全屏时暂停对应实例。活动概览中的预览
  可以继续播放。两个暂停选项可独立关闭。全屏暂停使用 Mutter 的每屏全屏状态。
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

偏好与播放策略在 GNOME 45.2、48.7 和 Ubuntu 26.04 的 GNOME 50.1 隔离会话中复测。
每个会话均验证了程序路径自动填写、视线开关、全屏停止提交帧、活动概览及退出全屏后的恢复。
GNOME 48.7 验证了两个选择对话框写入路径和 Vulkan 下的暂停恢复。跨输出坐标使用带负值的
输入样本验证，实体多屏尚未实测。对应日志在本地实验目录的 `mantic-settings/`、
`trixie-settings-final/`、`trixie-browse-final/` 和 `resolute-direct2/`。

从 Manjaro 26.1.0 GNOME ISO 提取的 GNOME Shell 50.4 / Mutter 50.4 在隔离无头会话中
用用户提供的 Ubuntu 22 CI 原包复测。OpenGL 软件渲染在默认扩展开启和关闭时均显示桌面
及活动概览，日志确认桌面持续提交帧。该隔离根文件系统缺少 Vulkan ICD，Vulkan 进程报
`cannot create Vulkan instance` 后退出。实机会话的显卡驱动及用户配置另需现场日志核对。
证据位于本地实验目录的 `manjaro-504-*`。

扩展元数据列出 GNOME 45–50。46、47 和 49 未实测。
49 起使用 Mutter 的新子进程接口，旧的任务列表隐藏方法不可用，任务切换器中的行为有待复核。
本次未验收物理多屏、分数缩放、锁屏恢复和硬件 GPU 性能。

隔离启动脚本、日志和截图保存在本地实验目录。交互复测截图分别位于
`mantic-desktop-input/` 和 `trixie-desktop-input/`。
在已配置的本地实验环境中，从仓库根目录运行下列命令可打开 1920×1080 的隔离窗口。
将 `48` 替换成 `45` 可切换 Shell 版本，末尾可指定 `opengl` 或 `vulkan`。

```bash
bash experiments/gnome-adapter-2026-11-20/launch-visible.sh wayland 48
```
