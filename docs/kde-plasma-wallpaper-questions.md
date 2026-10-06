# KDE Plasma 动态壁纸插件开发提问册

> 本文档供 ChatGPT 答疑使用。文档自包含，无需访问项目仓库。
> 请针对以下问题提供详细解答，包括代码示例、API 参考、最佳实践和注意事项。

---

## A. 项目背景（必读）

### A.1 NekoMote 是什么

NekoMote 是一个 **Linux 原生 E-mote 二维角色模型运行时**，从零独立实现（C++20），用于在桌面长期运行动态壁纸。它使用已有解包的 E-mote 素材（JSON + PNG 图集），独立实现参数求值、时间线动画、网格变形、遮罩混合、物理模拟、鼠标交互、触摸反应和音频唇形同步。

### A.2 技术栈

| 项目 | 选择 |
|------|------|
| 语言 | C++20 |
| 构建 | CMake 4.0.3 + Ninja 1.12.1 |
| 图形 | OpenGL 3.3+（当前环境 OpenGL 4.6, NVIDIA 575.64.03） |
| 窗口（开发用） | GLFW（仅预览/诊断，非产品） |
| 音频 | PulseAudio（独立 worker 线程，PCM 播放 + RMS 采样） |
| PNG 解码 | stb_image |
| JSON | nlohmann/json |
| 测试 | doctest |

### A.3 当前运行环境

| 项目 | 版本 |
|------|------|
| Plasma | 6.3.6 |
| Qt | 6.9.1 |
| KF | 6.x（具体版本待确认，pkg-config 未检测到 KF6Plasma） |
| 会话类型 | X11（`XDG_SESSION_TYPE=x11`，无 Wayland） |
| GPU | NVIDIA, OpenGL 4.6.0, 驱动 575.64.03 |
| GCC | 15.1.1 |
| Clang/clang-format | 20.1.8 |

### A.4 引擎当前状态

- **核心引擎已完成**：参数求值、时间线、差分动画、网格变形、遮罩/混合、物理模拟、反应状态机、音频唇形同步、鼠标注视/触摸交互全部实现。
- **性能**：稳态零堆分配，帧时间 425-768μs/帧（取决于模型复杂度），30/60 FPS 可选。
- **测试**：127 个单元测试全通过，192/194 型号全目录审计通过。
- **已有应用**：`nekomote_player`（GLFW 普通窗口预览，支持鼠标交互+音频+背景），`nekomote_preview`（诊断预览）。
- **未实现**：运行时模型切换、背景时段切换、控制菜单、桌面适配器。

### A.5 目标

制作 **KDE Plasma 6 桌面壁纸插件**，让 NekoMote 角色显示在桌面背景上，支持：
- 鼠标注视（角色眼睛/头部跟随鼠标）
- 触摸反应（点击角色不同部位触发动画反应）
- 音频播放（反应配音 + 唇形同步）
- 配置界面（选择模型、背景图、FPS、音频开关等）
- 多屏支持
- 低功耗（不可见时暂停、电池模式降帧）

---

## B. 引擎接口定义（必读）

### B.1 核心类

引擎分两层：**不可变编译资产**（`SessionProgram`）和**可变实例状态**（`SessionInstance`）。

```cpp
namespace nekomote::application {

// 不可变编译资产。编译后源数据可卸载。GPU 资源由宿主管理。
class SessionProgram {
public:
    ~SessionProgram();
    SessionProgram(const SessionProgram&) = delete;
    // 底层运行时程序（场景图、动画、物理）
    const runtime::PlaybackProgram& playback() const noexcept;
    // 反应音频文件路径（相对 session 目录）
    std::span<const std::string> audio_paths() const noexcept;
    // 按标签查找反应
    ReactionId find_reaction(std::string_view label) const noexcept;
    std::string_view reaction_label(ReactionId id) const noexcept;
    // 能力查询
    bool gaze_available() const noexcept;   // 是否支持鼠标注视
    bool touch_available() const noexcept;  // 是否支持触摸反应
};

// 编译结果
struct SessionCompileResult {
    std::unique_ptr<const SessionProgram> program;
    std::vector<std::string> errors;
};
// 从模型资产 + session 资产编译
SessionCompileResult compile_session(const model::ModelAsset&,
                                     const model::SessionAsset&);

// 可变实例状态。单线程求值。借用 SessionProgram，program 必须比实例长寿。
// 输入坐标为居中输出像素（Y 向下）。宿主自行转换表面坐标。
class SessionInstance {
public:
    explicit SessionInstance(const SessionProgram&, SessionConfig config = {});
    ~SessionInstance();

    // 生命周期
    SessionStatus start() noexcept;
    SessionStatus advance(double elapsed_seconds) noexcept;  // 推进动画/物理
    SessionStatus prepare_frame() noexcept;                   // 求值最终参数，构建渲染帧
    RenderFrame frame() const noexcept;                       // 获取渲染协议帧
    SessionStatus reset() noexcept;

    // 配置
    SessionStatus set_origin(Vec2 origin) noexcept;  // 模型原点（输出像素）
    SessionStatus set_scale(float scale) noexcept;   // 模型缩放

    // 交互（坐标为居中输出像素，Y 向下）
    SessionStatus pointer_move(Vec2 position) noexcept;
    SessionStatus pointer_leave() noexcept;
    SessionStatus pointer_enter(Vec2 position) noexcept;
    SessionStatus touch(Vec2 position) noexcept;      // 触发反应
    SessionStatus react(ReactionId id) noexcept;      // 直接触发指定反应

    // 音频
    SessionStatus set_audio_level(float rms) noexcept;  // 0..1, 来自 PCM RMS
    std::optional<AudioCommand> pop_audio_command() noexcept;  // Play/Stop/Pause/Resume

    // 状态查询
    ReactionPhase reaction_phase() const noexcept;  // Idle/Playing/Recovering
    ReactionId active_reaction() const noexcept;
    const runtime::PlaybackInstance& playback() const noexcept;
};

} // namespace nekomote::application
```

### B.2 配置与状态

```cpp
struct SessionConfig {
    runtime::AnimationConfig animation;
    std::uint32_t reaction_seed{0};           // 反应随机种子
    PointerScope pointer{PointerScope::Unavailable};  // Unavailable / LocalSurface
    AudioMode audio{AudioMode::Disabled};     // Disabled / Device
};

enum class ReactionPhase { Idle, Playing, Recovering };
enum class AudioCommandKind { Play, Stop, Pause, Resume };
struct AudioCommand { AudioCommandKind kind; AudioClipId clip; };

struct SessionStatus {
    SessionError error{SessionError::None};  // None/InvalidInput/InvalidReaction/Busy/InputUnavailable/EventQueueFull/Playback/Faulted
    runtime::PlaybackStatus playback;
    explicit operator bool() const noexcept { return error == SessionError::None; }
};
```

### B.3 渲染帧协议

`frame()` 返回非拥有视图，在下一次 `advance()`/`prepare_frame()` 前有效。

```cpp
namespace nekomote {
// 像素/模型 XY（Y 向下）；左上角归一化 UV；straight color/opacity
struct RenderVertex {
    float x, y, u, v;
    std::array<float, 4> color{1, 1, 1, 1};
};

enum class BlendMode { Over, Add, Multiply, Screen };
enum class ColorComposition { PremultipliedOver, ProtectedStraight };

struct DrawCommand {
    TextureId texture;           // 纹理 ID（one-based）
    std::uint32_t first_index, index_count;
    BlendMode blend{BlendMode::Over};
    MaskId mask{};               // 遮罩 ID（0 = 无遮罩）
};

// 非拥有视图；caller 保持存储有效直到 prepare 返回
struct RenderFrame {
    std::span<const RenderVertex> vertices;
    std::span<const std::uint32_t> indices;
    std::span<const DrawCommand> commands;
    ColorComposition composition{ColorComposition::PremultipliedOver};
    std::span<const DrawCommand> mask_commands{};
    std::span<const MaskDefinition> masks{};
};
}
```

### B.4 每帧管线

```
host loop:
  1. session.advance(elapsed_seconds)   // 推进动画时钟、物理、反应状态机
  2. session.prepare_frame()            // 求值最终参数，构建 RenderFrame
  3. frame = session.frame()            // 获取顶点/索引/命令
  4. GPU: 上传 vertices/indices，按 commands 顺序绘制（含遮罩）
  5. audio: pop_audio_command() → 播放/停止音频
  6. audio: set_audio_level(rms)        // 回写 PCM RMS 用于唇形同步
```

**关键约束**：
- `advance()` + `prepare_frame()` + `frame()` 必须在同一线程调用（单线程求值）。
- `frame()` 返回的 span 在下一次 `advance()`/`prepare_frame()` 后失效。
- GPU 绘制由宿主执行，引擎不做任何 OpenGL 调用。
- 纹理由宿主上传和管理，`TextureId` 是 one-based 索引，宿主维护 ID → GL 纹理映射。

### B.5 资产加载

```cpp
// 离线导入：E-mote JSON + PNG → 项目资产 JSON
// tools/import_emote: 导入 CLI
// tools/import_session: session 资产编译

// 运行时加载
namespace nekomote::asset_io {
struct AssetLoadResult {
    bool ok;
    model::ModelAsset asset;
    std::vector<std::string> errors;
};
AssetLoadResult load_asset(const std::string& model_path);  // .nkm.json

struct SessionAssetLoadResult {
    bool ok;
    model::SessionAsset asset;
    std::vector<std::string> errors;
};
SessionAssetLoadResult load_session_asset(const std::string& session_path);  // .nks.json
}
```

### B.6 线程模型

- **求值线程**：`advance()` + `prepare_frame()` + `frame()` 单线程。
- **音频线程**：PulseAudio worker 独立线程，通过命令队列/RMS 快照与求值线程交互。
- **GPU 线程**：宿主控制，引擎不调用 OpenGL。
- **稳态零堆分配**：预热后 advance/prepare_frame/render 路径无常态堆分配。

---

## C. 问题清单

### C.1 Plasma 壁纸插件架构

1. **Plasma 6 壁纸插件的正式注册方式是什么？** metadata.json/metadata.desktop 的完整字段？安装路径（`~/.local/share/plasma/wallpapers/` vs `/usr/share/plasma/wallpapers/`）？请给出最小可用 metadata 示例。

2. **壁纸插件的完整目录结构是什么？** 需要哪些文件（metadata、QML、C++、CMakeLists.txt）？请给出最小目录树。

3. **Plasma 6 壁纸插件与 Plasma 5 的 API 差异？** Qt6/KF6 vs Qt5/KF5 的 breaking changes？是否需要 `#if` 编译条件？

4. **壁纸插件如何被 Plasma 壁纸系统发现和加载？** `kpackagetool6` 安装？`plasma-install_package` CMake 宏？重启 plasmashell 需要吗？

5. **壁纸插件的进程模型是什么？** 是 `plasmashell` 进程内的 .so 插件？还是独立进程？崩溃是否会影响整个桌面？

### C.2 渲染机制

6. **Plasma 6 壁纸的渲染目标是什么？** QQuickItem？QQuickFramebufferObject？自定义 QSGNode？壁纸插件如何获得绘制表面？

7. **如何在 Plasma 6 壁纸中做原生 OpenGL 渲染？** Qt6 的 RHI 抽象层（可能用 Vulkan/Metal）是否影响 OpenGL 可用性？`QQuickWindow::beginExternalRender()` / `beforeRenderPassRecording()` 的用法？

8. **NekoMote 引擎不调用 OpenGL，由宿主执行 GPU 绘制。** 壁纸插件如何获取 `RenderFrame` 的 vertices/indices/commands 并上传到 GPU？`QOpenGLBuffer` + `QOpenGLTexture` + `QOpenGLShaderProgram`？还是 QSGGeometryNode？

9. **壁纸的刷新机制是什么？** 如何请求重绘？`QQuickItem::update()`？壁纸是否始终活跃还是桌面不可见时自动暂停？

10. **如何控制壁纸帧率？** 30/60 FPS 的实现方式？`QTimer` vs vsync？`QQuickWindow` 的 frame callback？

11. **纹理管理：** NekoMote 的 `TextureId` 是 one-based 索引，宿主维护 ID→GL 纹理映射。壁纸插件如何管理这个映射？纹理何时上传、何时释放？

### C.3 QML / Qt Quick 接入

12. **壁纸插件的 QML 根元素是什么？** `WallpaperItem`？需要继承哪个类型？请给出最小 QML 壁纸示例。

13. **如何从 QML 调用 C++ 壁纸后端？** `Q_PROPERTY`、`Q_INVOKABLE`、信号槽？如何将 C++ 渲染对象暴露给 QML？

14. **C++ 渲染线程与 Qt Quick 渲染线程如何协调？** Qt Quick Scene Graph 的渲染线程是独立的？NekoMote 的 `advance()` 应在 GUI 线程还是渲染线程调用？`QSGNode::updatePaintNode` 的线程上下文？

15. **NekoMote 的 OpenGL 调用如何与 Qt6 RHI 共存？** Qt6 可能使用 Vulkan 后端，NekoMote 要求 OpenGL 3.3+。如何确保 OpenGL 上下文可用？`QOpenGLContext` vs `QRhi`？

16. **壁纸销毁时如何正确释放 GPU 资源？** `QQuickItem` 析构顺序？OpenGL 资源在哪个线程释放？`QOpenGLContext::aboutToBeDestroyed`？

### C.4 配置界面

17. **壁纸配置界面的标准实现方式是什么？** `config.qml`？`KConfig`/`KConfigSkeleton`？配置 UI 用 QML 还是 QWidget？

18. **如何让用户选择模型文件？** `FileDialog`？模型文件路径如何存储？路径是绝对路径还是相对路径？

19. **如何让用户选择背景图？** 同上。支持多背景轮换/时段切换？

20. **配置变更后如何热重载？** NekoMote 的 `SessionProgram` 是不可变的，模型切换需要重建 `SessionInstance`。如何安全地销毁旧实例并创建新实例？双缓冲策略？

21. **配置持久化格式？** `plasmarc`？`KConfig` ini？配置文件位置？

### C.5 鼠标/触摸交互

22. **Plasma 6 壁纸在 X11 下能否接收鼠标事件？** 壁纸窗口的 `_NET_WM_WINDOW_TYPE_DESKTOP` 属性是否阻止输入？`MouseArea` vs C++ 事件过滤器？

23. **Wayland 下壁纸能否接收鼠标事件？** layer-shell 的 Background layer 是否有 input 区域？KDE Wayland 的具体实现？

24. **桌面全局鼠标位置如何获取？** NekoMote 的注视功能需要全局鼠标位置（即使鼠标不在壁纸窗口内）。X11：`XQueryPointer`？Wayland：是否可行？`KWindowSystem`？

25. **鼠标坐标如何从全局坐标转换到壁纸局部坐标？** 多屏偏移？DPI 缩放？

26. **触摸事件在桌面壁纸层是否可用？** X11 和 Wayland 下的触摸支持？

### C.6 音频集成

27. **Plasma 壁纸中 PulseAudio 是否正常工作？** NekoMote 已有 PulseAudio 后端（独立 worker 线程），在壁纸进程中是否有权限限制？

28. **壁纸音频是否受桌面音量控制？** PulseAudio stream role/media category？`event` role 会被静音吗？如何设置 `media.role = "animation"`？

29. **锁屏/休眠时音频如何处理？** 自动暂停？恢复后如何同步？

### C.7 Wayland vs X11

30. **Plasma 6 在 Wayland 下壁纸插件的行为与 X11 有何不同？** 渲染机制、输入事件、多屏支持？

31. **Wayland 下壁纸的窗口层级？** `zwlr_layer_shell_v1` 的 Background layer？KDE 自定义协议？

32. **Plasma 壁纸插件是否自动处理 Wayland/X11 差异？** 还是需要插件代码手动判断？

33. **NekoMote 当前环境是 X11，但未来需要支持 Wayland。** 开发时需要预留哪些抽象？X11 专有 API（如 `XQueryPointer`）如何抽象？

### C.8 多屏支持

34. **Plasma 6 壁纸如何处理多屏？** 每屏一个壁纸实例？还是共享实例多目标渲染？

35. **多屏下壁纸几何如何获取？** `QScreen`？`QWindow::screen()`？屏幕热插拔事件？

36. **多屏下不同分辨率/DPI 如何处理？** device pixel ratio？NekoMote 的 `set_scale()` 如何配合？

37. **多屏下鼠标坐标如何映射？** 全局坐标到各屏幕壁纸局部坐标？

### C.9 生命周期与资源管理

38. **壁纸插件如何响应桌面重启？** `plasmashell` 重启后壁纸如何恢复？

39. **壁纸插件如何响应用户切换/注销？** 资源清理顺序？

40. **GPU 上下文丢失如何处理？** 驱动崩溃恢复？`QOpenGLContext::aboutToBeDestroyed`？NekoMote 的纹理如何重建？

41. **壁纸与桌面合成的 GPU 资源竞争？** 壁纸渲染与 KWin 合成器的资源隔离？显存预算？

### C.10 性能与功耗

42. **如何实现自适应帧率？** 桌面可见时 60FPS，不可见时暂停？如何检测桌面可见性？

43. **如何检测电池/电源状态？** `UPower` DBus？电池模式降帧/暂停物理？

44. **壁纸的 CPU/GPU 占用预算？** 稳态每帧 CPU 时间？NekoMote 当前 425-768μs/帧，是否足够？

45. **如何测量壁纸性能？** `QSG_RENDER_TIMING=1`？`perf`？`Hotspot`？`nvidia-smi`？

### C.11 打包与安装

46. **壁纸插件的 CMake 构建如何配置？** `plasma_install_package`？链接 NekoMote 共享库/静态库的 `target_link_libraries`？

47. **壁纸插件安装路径？** `/usr/share/plasma/wallpapers/nekomote/`？`~/.local/share/plasma/wallpapers/nekomote/`？

48. **如何打包为 Arch Linux PKGBUILD？** 依赖声明？`depends=(plasma-workspace qt6-base ...)`？

49. **如何将 NekoMote C++ 库与壁纸插件一起分发？** 共享库 vs 静态库？RPATH？`LD_LIBRARY_PATH`？

50. **模型素材如何分发？** 内置在插件包中？外部下载？版权声明？

### C.12 调试与诊断

51. **如何调试 Plasma 6 壁纸插件？** `QT_LOGGING_RULES`？`gdb` attach 到 `plasmashell`？独立 QML 测试？

52. **如何查看壁纸日志？** `journalctl`？`~/.xsession-errors`？

53. **如何独立测试壁纸 QML？** `qmlscene6` / `qml6` 命令？

54. **如何 profiling 壁纸性能？** `QSG_RENDER_TIMING=1`？`perf`？`Hotspot`？

### C.13 参考实现

55. **KDE 官方壁纸插件的参考实现？** `org.kde.plasma.wallpapers.image` 的源码位置？`org.kde.plasma.wallpapers.color`？

56. **是否有第三方动态壁纸插件参考？** 视频壁纸？`com.github.caspermeijn.plasma-wallpaper-videos`？其他 OpenGL 壁纸？

57. **是否有 `QQuickFramebufferObject` 在壁纸中使用的示例？** FBO 渲染 +C++ 后端？

58. **Plasma 6 壁纸插件的最小可用 hello world？** 请给出完整代码（metadata + QML + C++ + CMake）。

### C.14 NekoMote 特定接入问题

59. **NekoMote 的三步管线（advance → prepare_frame → frame）如何映射到 Qt Quick 渲染循环？** `beforeFrameBegin` / `beforeRenderPassRecording` / `afterRenderPassRecording` 的对应关系？

60. **NekoMote 的 `RenderFrame` 包含 vertices/indices/commands/masks。** 如何高效上传到 GPU 并按 commands 顺序绘制？`QOpenGLBuffer` 静态/动态更新？遮罩 FBO？

61. **NekoMote 的 `SessionProgram` 不可变，模型切换需重建 `SessionInstance`。** 在壁纸插件中如何安全重建？旧实例继续渲染，新实例后台编译，完成后切换？

62. **NekoMote 的注视功能需要全局鼠标位置。** X11 下 `XQueryPointer` 可行，Wayland 下如何？是否需要降级为"仅壁纸内鼠标"？

63. **NekoMote 的音频（PulseAudio）在壁纸进程中是否有权限限制？** PulseAudio 用户模式 vs 系统模式？

64. **NekoMote 当前用 GLFW 预览应用已有完整实现（鼠标交互+音频+背景）。** 壁纸插件能否复用预览应用的渲染逻辑？还是需要重写？

---

## D. 优先级标注

请按以下优先级标注每个问题的解答重要性：

- **[阻塞]**：不解决就无法开始 P8 开发
- **[重要]**：开发早期需要明确，影响架构设计
- **[后续]**：开发中后期或优化阶段需要
- **[参考]**：有最好，没有也能自行摸索

---

## E. 期望的解答格式

对于每个问题，请提供：
1. **直接答案**：是/否、API 名称、代码片段
2. **代码示例**：最小可用代码片段（C++/QML/CMake）
3. **注意事项**：常见陷阱、版本差异、性能影响
4. **参考链接**：Qt/KDE 官方文档 URL、源码位置

对于 C.14（NekoMote 特定问题），请结合 B 节的接口定义给出具体集成方案。
