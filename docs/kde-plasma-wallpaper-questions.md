# KDE Plasma 动态壁纸插件开发提问册

> 本文档整理 NekoMote 引擎接入 KDE Plasma 桌面壁纸需要了解的问题。
> NekoMote 已有独立 C++ 渲染引擎（OpenGL 3.3+，C++20），需要通过 Plasma 壁纸插件接入桌面。
> 请 ChatGPT 针对以下问题提供详细解答，包括代码示例、API 参考、最佳实践和注意事项。

## 背景

- **NekoMote 引擎**：独立 C++20 原生二维模型运行时，已有 OpenGL 3.3 后端、动画/物理/反应/音频/交互完整实现。
- **当前环境**：KDE Plasma（具体版本待确认），X11 会话，GCC 15.1.1，CMake 4.0.3。
- **引擎接口**：C++ 接口，`SessionProgram`（不可变编译资产）+ `SessionInstance`（可变实例状态），每帧 `advance(elapsed) → prepare_frame() → frame()`。
- **目标**：制作 Plasma 桌面壁纸插件，让 NekoMote 角色显示在桌面背景上，支持鼠标交互、触摸反应、音频播放。

---

## 一、Plasma 壁纸插件架构

### 1.1 插件类型与注册

1. **Plasma 壁纸插件的正式名称是什么？** 是 `org.kde.plasma.wallpapers.*` 还是其他？如何注册一个自定义壁纸插件？

2. **壁纸插件的目录结构是什么？** 需要哪些文件（metadata.json/metadata.desktop、QML 文件、CMakeLists.txt 等）？请给出完整的最小目录结构示例。

3. **metadata 文件的完整字段有哪些？** `KPlugin`、`X-KDE-ParentApp`、`X-Plasma-DropMimeTypes` 等字段的含义和必填项？壁纸插件特有的 metadata 字段（如 `X-KDE-PluginInfo-Category`）？

4. **壁纸插件如何被 Plasma 壁纸系统发现和加载？** 安装路径（`~/.local/share/plasma/wallpapers/` vs `/usr/share/plasma/wallpapers/`）？是否需要 `kpackagetool5` 或 `kpackagetool6` 安装？

5. **Plasma 5 vs Plasma 6 的壁纸插件 API 有何差异？** NekoMote 目标是 Plasma 6（Qt6）还是同时支持 Plasma 5（Qt5）？API 兼容性如何处理？

### 1.2 壁纸渲染机制

6. **Plasma 壁纸的渲染目标是什么？** 是 QQuickItem、QQuickPaintItem 还是其他？壁纸插件如何获得绘制表面？

7. **壁纸插件如何接入 OpenGL 渲染？** Qt Quick Scene Graph 的 `QSGGeometryNode` / `QSGSimpleTextureNode` / 自定义 `QSGNode`？如何创建 FBO 并将原生 OpenGL 渲染结果合成到 Qt Quick？

8. **Plasma 6 的壁纸渲染是否支持直接 OpenGL 调用？** 是否可以通过 `QOpenGLFramebufferObject` 或 Qt6 的 `QRhi` / `QQuickWindow::beginExternalRender()` 直接在壁纸中做 OpenGL 渲染？有哪些限制？

9. **壁纸的刷新机制是什么？** `render()` 调用频率？如何请求重绘（`update()` vs `QQuickItem::update()`）？壁纸是否始终活跃还是桌面不可见时暂停？

10. **如何控制壁纸的帧率？** 30/60 FPS 的实现方式？`QTimer` vs `vsync` 同步？壁纸帧率与屏幕刷新率的关系？

---

## 二、QML / Qt Quick 接入

### 2.1 QML 壁纸接口

11. **壁纸插件的 QML 根元素是什么？** `WallpaperItem`？需要继承哪个类型？请给出最小 QML 壁纸示例。

12. **如何从 QML 调用 C++ 壁纸后端？** `Q_PROPERTY`、`Q_INVOKABLE`、信号槽？如何将 C++ 渲染对象暴露给 QML？

13. **壁纸的配置界面如何实现？** `config.qml`？如何定义配置项（模型路径、背景图、FPS、音频开关等）？配置如何持久化？

14. **壁纸如何接收鼠标事件？** QML 的 `MouseArea` 还是 C++ 的事件过滤器？鼠标坐标如何从桌面全局坐标转换到壁纸局部坐标？

15. **壁纸如何接收触摸事件？** 触摸事件在桌面壁纸层是否可用？X11 和 Wayland 下触摸支持有何差异？

### 2.2 C++ 后端集成

16. **如何在 C++ 中创建自定义 QQuickItem 并嵌入 QML？** `QQuickItem` 子类化？`QSGNode` 的 `updatePaintNode()` 重写？

17. **C++ 渲染线程与 Qt Quick 渲染线程如何协调？** Qt Quick Scene Graph 的渲染线程是独立的？NekoMote 的 `advance()` 应在哪个线程调用？GUI 线程 vs 渲染线程？

18. **如何安全地在 Qt Quick 渲染线程中调用 NekoMote 的 OpenGL 渲染？** `QQuickWindow::beforeRenderPassRecording()` / `afterRenderPassRecording()`？Qt6 RHI 抽象层的影响？

19. **NekoMote 的 OpenGL 上下文与 Qt Quick 的 OpenGL 上下文是否共享？** 如何获取 Qt 的 OpenGL 上下文？`QOpenGLContext::currentContext()`？是否需要 `makeCurrent()`？

20. **如何在壁纸销毁时正确释放 GPU 资源？** `QQuickItem` 的析构顺序？OpenGL 资源在哪个线程释放？`QOpenGLContext::aboutToBeDestroyed` 信号？

---

## 三、Wayland vs X11

### 3.1 显示协议差异

21. **Plasma 6 在 Wayland 下壁纸插件的行为与 X11 有何不同？** 渲染机制、输入事件、多屏支持等方面？

22. **Wayland 下壁纸是否能接收鼠标事件？** Wayland 的 layer-shell 协议中，背景层是否能接收输入？`input` 区域如何设置？

23. **Wayland 下壁纸的窗口层级是什么？** `zwlr_layer_shell_v1` 的 `Background` layer？KDE 的 `ext-blur-v1` 或自定义协议？

24. **X11 下壁纸如何设置桌面属性？** `_NET_WM_WINDOW_TYPE_DESKTOP`？如何确保壁纸在桌面图标下方？

25. **Plasma 壁纸插件是否自动处理 Wayland/X11 差异？** 还是需要插件代码中手动判断？`KWindowSystem` 的作用？

### 3.2 多屏支持

26. **Plasma 壁纸如何处理多屏？** 每个屏幕一个壁纸实例？还是共享一个实例多目标渲染？

27. **多屏下壁纸的几何如何获取？** `QScreen`？`QWindow::screen()`？屏幕热插拔事件？

28. **多屏下不同分辨率/DPI 如何处理？** device pixel ratio？NekoMote 的 `set_scale()` 如何配合？

29. **多屏下鼠标坐标如何映射？** 全局坐标到各屏幕壁纸局部坐标的转换？

---

## 四、配置界面与用户交互

### 4.1 配置界面

30. **壁纸配置界面的标准实现方式是什么？** `ConfigModel` / `KConfig` / `KConfigSkeleton`？配置 UI 用 QML 还是 QWidget？

31. **如何让用户选择模型文件？** 文件选择对话框（`FileDialog`）？模型文件路径如何存储？

32. **如何让用户选择背景图？** 同上，背景图路径存储和切换。

33. **如何实现模型切换的配置项？** 下拉选择？从模型目录自动发现可用模型？

34. **配置变更后如何热重载？** 模型切换、背景切换是否需要重建 `SessionInstance`？如何安全地销毁旧实例并创建新实例？

35. **配置的持久化格式是什么？** `plasmarc`？`KConfig` 的 ini 格式？配置文件位置？

### 4.2 运行时控制

36. **壁纸是否支持右键菜单？** 桌面右键通常被 Plasma 占用，壁纸能否拦截或添加菜单项？

37. **如何实现壁纸内的控制菜单？** 悬浮按钮？系统托盘？DBus 接口？

38. **如何通过 DBus 控制壁纸？** 注册 DBus 服务？外部命令触发反应/切换模型？

39. **壁纸的暂停/恢复如何实现？** 桌面不可见时暂停？锁屏时暂停？如何检测桌面可见性？

---

## 五、音频集成

40. **Plasma 壁纸中如何播放音频？** NekoMote 已有 PulseAudio 后端，在壁纸进程中是否正常工作？

41. **壁纸进程的音频是否受桌面音量控制？** PulseAudio 的 stream role / media category 如何设置？`event` role 会被静音吗？

42. **锁屏/休眠时音频如何处理？** 自动暂停？恢复后如何同步？

43. **多壁纸实例的音频混合？** 如果多个屏幕各有一个壁纸实例，音频如何避免冲突？

---

## 六、生命周期与资源管理

### 6.1 进程生命周期

44. **壁纸插件的进程生命周期是什么？** 是独立进程还是 `plasmashell` 进程内的插件？崩溃是否会影响整个桌面？

45. **壁纸插件如何响应桌面重启？** `plasmashell` 重启后壁纸如何恢复？

46. **壁纸插件如何响应用户切换？** 注销/切换用户时壁纸如何清理？

### 6.2 GPU 资源管理

47. **壁纸的 GPU 资源预算是多少？** 显存占用上限？纹理上传策略？

48. **GPU 上下文丢失如何处理？** `GL_OUT_OF_MEMORY`？驱动崩溃恢复？`QOpenGLContext::aboutToBeDestroyed`？

49. **壁纸与桌面合成的 GPU 资源竞争？** 壁纸渲染与 KWin 合成器的资源隔离？

50. **如何测量壁纸的 GPU 资源占用？** `nvidia-smi`？`radeontop`？`intel-gpu-top`？

### 6.3 性能与功耗

51. **壁纸的 CPU 占用预算是多少？** 稳态每帧 CPU 时间？空闲时是否降帧？

52. **如何实现自适应帧率？** 桌面可见时 60FPS，不可见时暂停？电池模式降帧？

53. **如何检测电池/电源状态？** `UPower` DBus？电池模式下降帧/暂停物理？

54. **壁纸的功耗影响如何测量？** `powerstat`？`turbostat`？

---

## 七、打包与安装

### 7.1 构建与打包

55. **壁纸插件的 CMake 构建如何配置？** `plasma_install_package`？`install()` 目标？依赖 `KF5Plasma` / `KF6Plasma`？

56. **如何将 NekoMote 的 C++ 库链接到壁纸插件？** 共享库 vs 静态库？`target_link_libraries` 配置？

57. **壁纸插件的安装路径是什么？** `/usr/share/plasma/wallpapers/nekomote/`？`~/.local/share/plasma/wallpapers/nekomote/`？

58. **如何打包为 Arch Linux 包（PKGBUILD）？** 依赖声明？`depends=(plasma-workspace qt6-base ...)`

59. **如何打包为 Flatpak / Snap？** 桌面壁纸插件在沙箱中的限制？

### 7.2 分发

60. **如何将壁纸插件发布到 KDE Store（store.kde.org）？** 打包格式？审核流程？

61. **模型素材如何分发？** 内置在插件包中？外部下载？版权声明？

62. **如何处理不同 Plasma 版本的兼容性？** Plasma 5.x / 6.x 的 `#if` 编译条件？运行时版本检测？

---

## 八、调试与诊断

63. **如何调试 Plasma 壁纸插件？** `QT_LOGGING_RULES`？`gdb` attach 到 `plasmashell`？`qmlscene` 独立测试？

64. **如何查看壁纸的日志输出？** `journalctl`？`~/.xsession-errors`？

65. **如何独立测试壁纸 QML？** `qmlscene` / `qml` 命令加载壁纸 QML？

66. **如何 profiling 壁纸性能？** `qputenv("QSG_RENDER_TIMING=1")`？`perf`？`Hotspot`？

67. **如何测试壁纸在不同屏幕尺寸下的行为？** 虚拟屏幕？`QScreen` 模拟？

---

## 九、NekoMote 特定问题

### 9.1 引擎接入

68. **NekoMote 的 `SessionProgram` 是不可变的，模型切换需要重建。在壁纸插件中如何安全地重建？** 双缓冲（旧实例继续渲染，新实例在后台编译，完成后切换）？

69. **NekoMote 的 `advance()` + `prepare_frame()` + `frame()` 三步管线如何映射到 Qt Quick 的渲染循环？** `beforeFrameBegin()` / `beforeRenderPassRecording()` / `afterRenderPassRecording()` 的对应关系？

70. **NekoMote 的 OpenGL 调用如何与 Qt6 的 RHI 抽象层共存？** Qt6 可能使用 Vulkan/Metal/D3D 后端，NekoMote 要求 OpenGL。如何确保 OpenGL 上下文可用？

71. **NekoMote 的鼠标交互（注视/触摸）如何接入桌面全局指针？** 桌面全局鼠标位置 vs 壁纸局部坐标？Wayland 下全局指针是否可获取？

72. **NekoMote 的音频（PulseAudio）在壁纸进程中是否有权限限制？** PulseAudio 系统模式 vs 用户模式？

### 9.2 模型与素材

73. **壁纸插件如何发现和加载模型？** 固定路径？用户选择？`XDG_DATA_HOME` 下的标准路径？

74. **模型素材的版权如何处理？** NekoMote 使用的 E-mote 素材来自原项目，发布时需要哪些版权声明？

75. **模型文件的大小和加载时间？** 大模型（如 chocola-date-a）的加载时间是否会影响桌面启动？异步加载？

---

## 十、参考实现与示例

76. **KDE 官方有哪些动态壁纸插件的参考实现？** `org.kde.plasma.wallpapers.image`？`org.kde.plasma.wallpapers.color`？是否有第三方动态壁纸插件（如 `com.github.caspermeijn.plasma-wallpaper-videos`）？

77. **是否有 KDE 壁纸插件接入 OpenGL 渲染的示例？** `QQuickFramebufferObject` 在壁纸中的使用？

78. **是否有 KDE 壁纸插件接入外部 C++ 库的示例？** 链接外部共享库的壁纸插件？

79. **Plasma 6 壁纸插件的最小可用示例（hello world）是什么？** 请给出完整代码（metadata + QML + C++ + CMake）。

80. **是否有从 `QQuickItem` 直接做 OpenGL 渲染的 Qt 官方示例？** `QQuickItem` + `QSGNode` + `QSGGeometryNode` 的纹理映射示例？

---

## 附：NekoMote 引擎接口摘要

供答疑方了解引擎能力，以下为 NekoMote 当前 C++ 接口：

```cpp
// 不可变编译资产（编译后可卸载源数据）
class SessionProgram {
public:
    const runtime::PlaybackProgram& playback() const noexcept;
    std::span<const std::string> audio_paths() const noexcept;
    ReactionId find_reaction(std::string_view label) const noexcept;
    std::string_view reaction_label(ReactionId) const noexcept;
};

// 可变实例状态
class SessionInstance {
public:
    SessionInstance(const SessionProgram&, SessionConfig);
    SessionStatus start() noexcept;
    SessionStatus advance(double elapsed_seconds) noexcept;
    SessionStatus prepare_frame() noexcept;
    std::span<const ParameterValue> values() const noexcept;
    const RenderFrame& frame() const noexcept;
    // 交互
    SessionStatus pointer_move(Vec2) noexcept;
    SessionStatus pointer_leave() noexcept;
    SessionStatus pointer_enter(Vec2) noexcept;
    SessionStatus touch(Vec2) noexcept;
    SessionStatus react(ReactionId) noexcept;
    // 音频
    SessionStatus set_audio_level(float rms) noexcept;
    std::optional<AudioCommand> pop_audio_command() noexcept;
    // 配置
    SessionStatus set_origin(Vec2) noexcept;
    SessionStatus set_scale(float) noexcept;
};
```

渲染后端为 OpenGL 3.3+，每帧管线：`advance(dt) → prepare_frame() → frame() → GPU draw`。
稳态零堆分配，帧时间 425-768μs/帧（取决于模型复杂度）。
