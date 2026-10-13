# NekoMote × KDE Plasma 6 动态壁纸插件：Agent 开发参考

> 范围：2026-10-05的KDE/Qt API调查材料，保留来源与宿主技术参考；旧提问册和最小原型任务已清理。项目架构/优先级以[规范](../AGENTS.md)和[G31专项交接](wallpaper-application-handoff.md)为准，不执行本文中的旧阶段安排。
> 基准环境：Plasma 6.3.6、Qt 6.9.1、KF6、X11；未来兼容 Wayland。  
> 更新时间：2026-10-05。  
> 结论优先级：**[阻塞] / [重要] / [后续] / [参考]**。  
> 注意：KDE/Qt API 会随 Plasma 6 小版本变化。本文优先采用 Qt/KDE 官方文档和 KDE 官方源码；第三方项目仅作参考实现，不作为 API 契约。

---

## 0. 先给 Agent 的架构结论

如果只保留十条，请保留下面这些：

1. **Plasma 6 壁纸是 QML 包，不是“独立桌面窗口程序”。** 正式根类型是 `WallpaperItem`，包位于 `plasma/wallpapers/<plugin-id>`。
2. **不要把 `QQuickFramebufferObject` 作为新代码的首选。** Qt 6 明确将它视为 OpenGL-only 的 legacy 兼容类。
3. 对 NekoMote 当前“CPU 产生 `RenderFrame`，宿主负责 GPU”的接口，首选：
   - **方案 A（推荐）**：`QQuickItem + QSGGeometryNode + QSGMaterial`，尽量走 Qt Quick Scene Graph/RHI；
   - **方案 B**：`QQuickItem + QSGRenderNode`，当必须直接发 OpenGL/QRhi 命令时使用。
4. **不要假定 plasmashell 一定使用 OpenGL RHI。** Qt 6 Scene Graph 可以是 OpenGL/Vulkan 等。壁纸插件本身没有 `main()`，因此无法像独立应用那样可靠调用 `QQuickWindow::setGraphicsApi(OpenGL)` 来决定整个 plasmashell 的后端。
5. 因此，**如果你希望插件对 Plasma 6 的运行后端无侵入，最好把 NekoMote 渲染后端实现为 Scene Graph/RHI 友好，而不是绑死裸 OpenGL。**
6. `advance() → prepare_frame() → frame()` 必须保持同线程。推荐在**Qt Quick render/sync 侧**拥有 `SessionInstance`，而 GUI/QML 侧只提交输入快照和配置请求。
7. `RenderFrame` 的 span 在下一次 `advance/prepare_frame` 后失效；不要把 span 指针跨帧、跨线程缓存。
8. 多屏不要一开始做“一个实例跨多 surface”。**Plasma 的桌面语义是每个物理屏幕都有一个 Desktop/Containment**。最简单可靠的模型是“每个 wallpaper item/屏幕一个 `SessionInstance`”，不可变 `SessionProgram` 可进程内共享。
9. X11 可以做全局鼠标查询；**标准 Wayland 不向普通客户端提供全局指针坐标**。必须把 pointer source 抽象掉，Wayland 默认降级为 surface-local gaze。
10. C++ 崩溃发生在 `plasmashell` 进程内时，通常会把桌面壳一起带崩。壁纸插件必须把资产错误、着色器错误、音频错误设计成可恢复错误，而不是 `abort/assert/throw through Qt`。

推荐模块划分：

```text
NekoMote Core (已有，平台无关)
├── SessionProgram           # immutable, 可共享
├── SessionInstance          # per wallpaper instance / per screen
└── RenderFrame              # CPU-side render protocol

Plasma Adapter
├── NekoWallpaperItem        # QQuickItem，QML 暴露层
├── NekoRenderNode/Nodes     # render thread only
├── ModelLoader              # 后台 I/O + compile_session
├── TextureCache             # per graphics context/window
├── PointerProvider
│   ├── X11GlobalPointerProvider
│   └── LocalSurfacePointerProvider
├── AudioBridge              # 复用已有 PulseAudio worker
└── PowerPolicy              # UPower / visibility / FPS policy
```

---

# C.1 Plasma 壁纸插件架构

## 1. [阻塞] Plasma 6 壁纸插件的正式注册方式、metadata 和安装路径

### 直接答案

Plasma 6 的壁纸插件是 **KPackage 类型的 Plasma/Wallpaper 包**。KDE 开发文档明确列出：

- 系统：`/usr/share/plasma/wallpapers/`
- 用户：`~/.local/share/plasma/wallpapers/`

根入口通常是：

```text
<package>/
├── metadata.json
└── contents/
    └── ui/
        └── main.qml
```

Plasma 6 应使用 `metadata.json`。旧项目中的 `metadata.desktop` 属于 Plasma 5/更旧时期常见格式，不建议为新 Plasma 6 插件继续创建两套元数据。

建议最小 metadata：

```json
{
  "KPlugin": {
    "Id": "uk.nekopara.nekomote",
    "Name": "NekoMote",
    "Description": "Native E-mote animated wallpaper",
    "Version": "0.1.0",
    "License": "GPL-3.0-or-later",
    "Website": "https://example.invalid/nekomote",
    "Authors": [
      {
        "Name": "NekoMote contributors"
      }
    ]
  },
  "KPackageStructure": "Plasma/Wallpaper"
}
```

不要机械复制旧 Plasma 5 示例中的 `X-Plasma-API`, `X-KDE-ServiceTypes` 等字段，除非当前安装的 Plasma 源码/模板明确要求。

### 注意事项

`Id` 最好与安装目录名相同，例如：

```text
~/.local/share/plasma/wallpapers/uk.nekopara.nekomote/
```

### 参考

- KDE Plasma themes/plugins: https://develop.kde.org/docs/plasma/
- KDE `WallpaperItem`: https://api.kde.org/qml-org-kde-plasma-plasmoid-wallpaperitem.html
- KDE 官方 wallpaper 源码目录：  
  https://invent.kde.org/plasma/plasma-workspace/-/tree/master/wallpapers  
  镜像：https://github.com/KDE/plasma-workspace/tree/master/wallpapers

---

## 2. [阻塞] 完整目录结构

建议把“Wallpaper KPackage”和“C++ QML 模块”区分开：

```text
nekomote-plasma-wallpaper/
├── CMakeLists.txt
├── package/
│   ├── metadata.json
│   └── contents/
│       ├── ui/
│       │   ├── main.qml
│       │   └── config.qml
│       └── config/
│           └── main.xml
└── src/
    ├── CMakeLists.txt
    ├── nekowallpaperitem.h
    ├── nekowallpaperitem.cpp
    ├── nekorendernode.h
    ├── nekorendernode.cpp
    └── ... 
```

如果做独立 QML module：

```text
src/
└── qml/
    └── org/nekomote/wallpaper/
        ├── qmldir            # CMake 自动生成时无需手写
        └── lib...so
```

QML package 本身不要求 C++；但 NekoMote 显然需要 C++ backend。

---

## 3. [重要] Plasma 6 vs Plasma 5 API 差异

核心变化：

- Qt 5 → Qt 6
- KF5 → KF6
- QML import 普遍允许不写版本：
  ```qml
  import QtQuick
  import org.kde.plasma.plasmoid
  ```
- Qt Quick Scene Graph 在 Qt 6 基于 RHI，不再默认等同于 OpenGL。
- `QQuickFramebufferObject` 仍存在，但仅在 Qt Quick 使用 OpenGL backend 时有效，被 Qt 官方视为 legacy。
- Qt 6 自定义 Scene Graph 材质 API 与 Qt 5 有明显变化；旧 `QSGSimpleMaterial` 已移除。
- `QQuickItem::geometryChanged()` 改为 `geometryChange()`。
- 自定义原生渲染时，Qt 6 要使用 `beginExternalCommands()/endExternalCommands()`，不能照抄 Qt 5 的 `resetOpenGLState()` 模式。

### 是否需要 `#if QT_VERSION`？

如果目标明确只支持 Plasma 6：**不需要兼容 Plasma 5。**  
强行一套源代码同时支持 Plasma 5/6 会显著扩大 QML、CMake、Scene Graph API 的维护面，不建议。

---

## 4. [阻塞] 发现、安装与重载

系统通过 KPackage 搜索路径发现 `Plasma/Wallpaper` 包。

开发期建议：

```bash
kpackagetool6 --type Plasma/Wallpaper --install ./package
```

更新：

```bash
kpackagetool6 --type Plasma/Wallpaper --upgrade ./package
```

删除：

```bash
kpackagetool6 --type Plasma/Wallpaper --remove uk.nekopara.nekomote
```

实际支持的 type 名称可先查：

```bash
kpackagetool6 --list-types
```

CMake 的 KDE 官方用法：

```cmake
find_package(Plasma REQUIRED)

plasma_install_package(
    package
    uk.nekopara.nekomote
    wallpapers
    wallpaper
)
```

KDE 自己的 Image wallpaper 也采用：

```cmake
plasma_install_package(imagepackage org.kde.image wallpapers wallpaper)
```

QML/metadata 改动后，开发期最可靠的重新装载方式仍是重新选择壁纸或重启 `plasmashell`。不要在产品逻辑里依赖“kill plasmashell”作为刷新手段。

---

## 5. [重要] 进程模型

Wallpaper QML 与其 C++ QML 扩展通常加载在 **plasmashell 进程**中，不是 NekoMote 独立子进程。

因此：

- C++ 野指针 / GL context misuse / render-thread race 可以导致整个 plasmashell 崩溃。
- 音频 worker 是你的线程，但仍属于 plasmashell 进程。
- 不应在插件中调用 `exit()`、修改全局 Qt Quick graphics API、安装进程级危险 signal handler。
- debug build 的 `assert()` 也需要谨慎。

如果未来特别关注隔离，可以设计“外部渲染 daemon + IPC + shared texture/共享内存”，但这会大幅增加复杂度；P8 第一版不建议。

---

# C.2 渲染机制

## 6. [阻塞] Plasma 6 壁纸的渲染目标

QML 根是 `WallpaperItem`，它继承 `QQuickItem`。你不是直接拿到“桌面 framebuffer”，而是加入 plasmashell 的 Qt Quick Scene Graph。

推荐路径：

```text
WallpaperItem (QML)
└── NekoWallpaperItem (C++ QQuickItem)
    └── updatePaintNode()
        └── QSGNode subtree
```

不要把 wallpaper 理解成 `_NET_WM_WINDOW_TYPE_DESKTOP` 独立窗口；那是外层桌面/containment 的实现细节，不是 wallpaper 插件 API。

---

## 7. [阻塞] Plasma 6 内做原生 OpenGL，以及 RHI 影响

Qt 6 Quick 可以使用 OpenGL/Vulkan/Metal/D3D。Linux Plasma 中实际后端也不应由 wallpaper 插件假定。

Qt 官方允许原生图形命令通过：

- `QQuickWindow::beforeRendering()`
- `QQuickWindow::beforeRenderPassRecording()`
- `QQuickWindow::beginExternalCommands()`
- `QQuickWindow::endExternalCommands()`
- 或 `QSGRenderNode`

但是“裸 OpenGL”成立的前提是当前 Scene Graph backend 本身允许你获得/使用 OpenGL context。

**关键问题：Wallpaper 插件没有应用 `main()`，不能安全地全局强制 plasmashell 改用 OpenGL。**

因此优先建议：

1. 先实现 QSG/RHI-neutral renderer。
2. 若短期必须复用 OpenGL renderer：
   - 检查 `window()->rendererInterface()->graphicsApi()`；
   - 仅在 OpenGL 时启用；
   - 非 OpenGL 后端显示清晰错误或 fallback；
   - 不要在插件内部强改 plasmashell graphics API。

---

## 8. [阻塞] RenderFrame 如何上传 GPU

### 推荐：QSGGeometryNode + 自定义 QSGMaterial

NekoMote 的帧协议天然适合 CPU geometry → scene graph。

思路：

```cpp
class NekoWallpaperItem : public QQuickItem {
    Q_OBJECT
public:
    NekoWallpaperItem() {
        setFlag(ItemHasContents, true);
    }

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode,
                             UpdatePaintNodeData *) override;
};
```

在 `updatePaintNode()` 中：

- 从 render-thread-owned `SessionInstance` 产生当前 `RenderFrame`
- 按 draw command 构造/更新 node
- 拷贝 vertices/indices 到 `QSGGeometry`
- texture 由 `QSGTexture`/material 管理
- blend/mask 映射到 material/pass

### 为什么不首选 QOpenGLBuffer？

因为它把 adapter 直接绑死 OpenGL；而 QSGGeometry/QSGMaterial 可以由 Qt Quick renderer 映射到底层 RHI。

### 遮罩

你现在的 `mask_commands + masks` 可能需要：

- stencil-based scene graph material/render node，或
- offscreen mask texture + shader sampling。

如果 mask 语义复杂、多个 pass 且需要精确控制 draw order，`QSGRenderNode` 可能比大量 `QSGGeometryNode` 更自然。

---

## 9. [重要] 刷新机制

对自定义 `QQuickItem`：

```cpp
setFlag(QQuickItem::ItemHasContents, true);
update();
```

`update()` 只请求下一次 scene graph update，不是立即渲染。

Qt 文档明确指出：只有 item 在显示状态时才会进行对应的 `updatePaintNode()` 更新。

不要建立一个不管可见性永远 busy-loop 的 render thread。

---

## 10. [重要] 30/60 FPS

不要用“精确 16.666 ms `QTimer` = vsync”的假设。

建议两级逻辑：

```text
Qt Quick render opportunity / update()
        ↓
FramePolicy 判断这次要不要推进
        ↓
steady_clock 计算真实 dt
        ↓
advance(dt)
```

30 FPS 可以隔帧或基于 monotonic deadline 跳帧：

```cpp
if (now < nextFrameDeadline)
    return;
nextFrameDeadline += 33.333ms;
item->update();
```

Qt `QScreen` 文档也明确建议不要用屏幕 refreshRate + timer 去驱动动画，应使用窗口更新机制。

注意：物理模拟最好使用真实 elapsed 或 capped dt，而不是固定写死 `1/30`。

---

## 11. [重要] TextureId → GPU texture 映射

建议：

```cpp
struct TextureResource {
    TextureId id;
    std::unique_ptr<QSGTexture> texture;
    QSize size;
};

std::vector<TextureResource> textures; // index = TextureId - 1
```

生命周期：

1. 模型 compile/load 完成后，CPU image 数据准备好。
2. 到 render thread / scene graph resource stage 创建 GPU texture。
3. `SessionProgram` 可以跨实例共享，但 GPU texture 是否能共享取决于 Qt Quick window/context/resource model。
4. screen/item/window 销毁时释放 render resources。
5. scene graph invalidated 后，认为 GPU cache 全部失效；CPU-side decoded asset 保留，按需重建。

不要把裸 `GLuint` 存进 `SessionProgram`，这会破坏“GPU 资源由宿主管理”的现有边界。

---

# C.3 QML / Qt Quick 接入

## 12. [阻塞] QML 根元素

Plasma 6 官方根类型：

```qml
import QtQuick
import org.kde.plasma.plasmoid
import NekoMote.Wallpaper

WallpaperItem {
    id: root

    NekoWallpaperItem {
        anchors.fill: parent
        modelPath: root.configuration.modelPath
        targetFps: root.configuration.fps
        audioEnabled: root.configuration.audioEnabled
    }
}
```

官方 `org.kde.image`、Haenau/Hunyango wallpaper 都以 `WallpaperItem` 为根。

---

## 13. [阻塞] QML 调 C++

标准 Qt QML 接法：

```cpp
class NekoWallpaperItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString modelPath
               READ modelPath
               WRITE setModelPath
               NOTIFY modelPathChanged)

    Q_PROPERTY(int targetFps
               READ targetFps
               WRITE setTargetFps
               NOTIFY targetFpsChanged)

    Q_PROPERTY(bool audioEnabled
               READ audioEnabled
               WRITE setAudioEnabled
               NOTIFY audioEnabledChanged)

public:
    Q_INVOKABLE void triggerReaction(const QString &label);
signals:
    void modelPathChanged();
    void loadError(const QString &message);
};
```

CMake 建议使用 `qt_add_qml_module()` 建 QML module，而不是在 `main.qml` 中手工 dlopen。

---

## 14. [阻塞] GUI 线程 / render thread / NekoMote 单线程约束

Qt Quick threaded render loop 中：

- GUI thread 处理 QML、输入、属性。
- render thread 做 scene graph sync/render。
- `updatePaintNode()` 在 render thread 调用，同时 GUI thread 被阻塞，因此这是安全同步点。

对 NekoMote 最简单的所有权模型：

```text
GUI thread:
  输入事件 -> 写 pending input/config snapshot
  model switch -> 发“准备新程序”请求
  不触碰当前 SessionInstance

render thread:
  独占 current SessionInstance
  consume snapshot
  advance(dt)
  prepare_frame()
  frame()
  update scene graph
```

这样严格满足：

```text
advance + prepare_frame + frame 同线程
```

不要一半在 GUI thread `advance()`，一半在 render thread `frame()`。

---

## 15. [阻塞] OpenGL 与 Qt6 RHI 共存

### 推荐决策

将“渲染协议解释器”与“具体 GPU API”再分一层：

```text
RenderFrame
  ├── QSG/RHI renderer   ← Plasma 6 首选
  └── OpenGL renderer    ← GLFW preview / fallback
```

当前 GLFW OpenGL 代码可以继续用于 `nekomote_player`，但 Plasma adapter 不应直接假定同一 backend。

如果采用 `QSGRenderNode`，可以在节点内部根据 `rendererInterface()->graphicsApi()` 选择 OpenGL 或未来 QRhi path。

`QQuickRhiItem` 是 Qt 新版对 QQuickFramebufferObject 的 RHI 替代方向，但需要确认你的最低 Qt 6.9 目标是否提供你所需 API，并且 Plasma 的打包环境是否适合；不要为了一个新类破坏最低版本兼容。

---

## 16. [重要] 销毁 GPU 资源

优先让 GPU 资源属于 scene graph node/tree，让 Qt 在正确线程清理。

自定义 `QQuickItem` 可重写：

```cpp
void releaseResources() override;
```

并处理：

```cpp
connect(window(), &QQuickWindow::sceneGraphInvalidated,
        ...,
        Qt::DirectConnection);
```

对于裸 OpenGL：

- 只在 render thread 且 context current 时删除 GL object。
- `aboutToBeDestroyed` 可作为底层 context 生命周期信号，但不要仅依赖它。
- 不能在 `QQuickItem` 普通 GUI-thread 析构函数中无条件 `glDelete*()`。

---

# C.4 配置界面

## 17. [阻塞] 标准配置结构

Plasma **wallpaper** 的配置布局与普通 plasmoid 教程略有不同。以 KDE 当前官方 Image/POTD wallpaper 源码为准，推荐：

```text
contents/
├── config/
│   └── main.xml
└── ui/
    ├── main.qml
    └── config.qml
```

这里要特别注意：普通 plasmoid 常见 `contents/config/config.qml + ui/configGeneral.qml` 的多页配置模型；当前 Plasma 6 官方 wallpaper（例如 `org.kde.image`、`org.kde.potd`）直接使用 `contents/ui/config.qml` 作为壁纸配置 UI。不要把 plasmoid 教程的目录结构原样套给 wallpaper。

`main.xml`：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<kcfg xmlns="http://www.kde.org/standards/kcfg/1.0">
  <kcfgfile name=""/>
  <group name="General">
    <entry name="modelPath" type="Path">
      <default></default>
    </entry>
    <entry name="backgroundPath" type="Path">
      <default></default>
    </entry>
    <entry name="fps" type="Int">
      <default>60</default>
    </entry>
    <entry name="audioEnabled" type="Bool">
      <default>true</default>
    </entry>
  </group>
</kcfg>
```

壁纸运行时读取：

```qml
root.configuration.modelPath
```

配置 UI 使用 QML/Kirigami/QtQuick.Controls，不要新做 QWidget KCM。

---

## 18. [重要] 模型文件选择

配置页可以使用 Qt Quick Dialogs：

```qml
import QtQuick.Dialogs

FileDialog {
    id: modelDialog
    title: i18n("Select NekoMote model")
    nameFilters: ["NekoMote model (*.nkm.json)", "All files (*)"]
    onAccepted: cfg_modelPath = selectedFile.toString()
}
```

路径策略：

- 用户任意文件：保存 canonical `file://` URL 或绝对 path。
- 插件内置资源：不要硬编码 `/usr/share`；用 package/relative URL。
- KConfig 的 `Path` 类型对 `$HOME` 有特殊序列化支持。

后端必须处理“文件被删、挂载点消失、权限改变”。

---

## 19. [后续] 背景图、多背景、时段切换

第一版建议只做一个背景路径。

后续可把策略设计成：

```cpp
struct BackgroundRule {
    QString path;
    QTime from;
    QTime to;
};
```

不要把轮换计时和 model runtime 混在一起；它属于 wallpaper adapter/policy。

---

## 20. [重要] 配置变化与热重载

模型切换不要在 render thread 直接同步做磁盘 I/O + JSON compile。

推荐状态机：

```text
Current:
  shared_ptr<const SessionProgram> programA
  unique_ptr<SessionInstance> instanceA

Loader worker:
  load model/session
  compile_session()
  decode images
        ↓
ReadyProgram B (CPU only)
        ↓ synchronized handoff
render thread:
  create instanceB
  upload B GPU resources
  first valid frame ready
        ↓
atomic/sync-point swap
A -> B
        ↓
release A scene graph/GPU resources
```

“后台编译 + 帧边界切换”是正确方向。

不要跨线程把 `SessionInstance` 从 worker 构造后直接拿去 render thread 使用，除非类明确定义可跨线程 move 且没有 thread-affine 成员。更稳的是 worker 只创建 immutable `SessionProgram` 和 CPU asset。

---

## 21. [重要] 配置持久化位置

Plasma 配置通常最终写在：

```text
~/.config/plasma-org.kde.plasma.desktop-appletsrc
```

WallpaperItem 内部会使用 containment 的 `Wallpaper/<plugin id>` 配置组加载 `main.xml` schema。

因此不要再造一个 `~/.config/nekomote-wallpaperrc` 保存普通 wallpaper UI 配置，除非那是跨桌面实例共享的独立全局配置。

---

# C.5 鼠标 / 触摸

## 22. [重要] X11 下壁纸能否接收鼠标事件

可以接收属于该 QQuick scene/desktop surface 的事件。壁纸内容是 QQuickItem，可用：

```qml
HoverHandler {
    onPointChanged: ...
}

TapHandler {
    onTapped: ...
}
```

或 C++：

```cpp
setAcceptHoverEvents(true);
setAcceptedMouseButtons(Qt::AllButtons);
```

但是注意桌面 containment、图标、widgets 会参与 hit-test；不要假定 wallpaper 永远得到桌面上的每次 click。

不应直接根据 `_NET_WM_WINDOW_TYPE_DESKTOP` 推导 wallpaper 的 Qt Quick 事件行为。

---

## 23. [重要] Wayland 下壁纸输入

对于 Plasma 自己内部的 wallpaper QML，输入首先由 plasmashell/KWin/Qt 处理；插件通常只需要处理 Qt Quick local input。

不要自己为 wallpaper 创建 `zwlr_layer_shell_v1` background surface。Plasma wallpaper 插件不是 wlr-layer-shell 客户端。

Wayland 的核心限制是：普通 client 获取的是其 surface 上的局部输入；无法像 X11 一样任意查询整个桌面的 global pointer。

---

## 24. [阻塞] 全局鼠标位置

### X11

可通过 X11/XCB 查询全局 pointer。若使用 Xlib：

```cpp
XQueryPointer(display, root, &rootReturn, &childReturn,
              &rootX, &rootY, &winX, &winY, &mask);
```

更建议隔离为 backend，而不是散落在 wallpaper 类中。

### Wayland

标准 Wayland **不能可靠获得全局 pointer position**。协议模型明确：

- client 不知道其 surface 的全局位置；
- pointer event 是 surface-local 坐标。

所以未来 Wayland 策略：

```cpp
enum class PointerCapability {
    GlobalDesktop,
    LocalSurface,
    Unavailable
};
```

NekoMote 已有 `PointerScope::LocalSurface`，这正好是合理降级。

---

## 25. [重要] 全局到局部坐标、多屏、DPI

X11 global logical coordinate：

```text
localLogical = globalLogical - screenGeometry.topLeft()
```

再转 NekoMote 输出坐标：

```cpp
float px = localLogical.x() * dpr;
float py = localLogical.y() * dpr;

Vec2 centered {
    px - framebufferWidth  * 0.5f,
    py - framebufferHeight * 0.5f
};
```

但 Qt 6 mixed-DPI 下不要自己假定所有 screen 坐标都是物理像素。以 wallpaper item/window 的实际逻辑尺寸和 `QWindow::devicePixelRatio()` 为准。

最重要的是建立一个统一函数：

```cpp
Vec2 surfacePointToNeko(QPointF qmlPoint,
                        QSizeF itemLogicalSize,
                        qreal dpr);
```

不要让不同输入路径各写一套转换。

---

## 26. [后续] 触摸事件

Qt Quick 支持 `TapHandler`、`PointHandler`、`MultiPointTouchArea` 等输入 abstraction；触摸能否到达 wallpaper 仍取决于 desktop containment/input routing。

NekoMote 当前只需要单点 `touch(Vec2)`，建议将 mouse click 和 touch tap 都归一化为：

```cpp
struct PointerTap {
    Vec2 local;
    PointerDevice device;
};
```

Wayland 不需要自己处理 `wl_touch`。

---

# C.6 音频

## 27. [重要] plasmashell 内使用 PulseAudio

普通用户会话内，插件线程连接 PulseAudio/PipeWire-Pulse 与普通应用没有本质权限差异。

但是它运行在 plasmashell 进程中，因此：

- 不要修改全进程 Pulse 环境变量作为唯一配置手段；
- 尽量给你创建的 stream 单独设置 proplist；
- worker 崩溃/死锁不能阻塞 plasmashell GUI/render thread。

---

## 28. [重要] 音量与 media.role

PulseAudio 支持 `PA_PROP_MEDIA_ROLE`，标准 role 包括：

```text
video
music
game
event
phone
animation
production
a11y
```

NekoMote 的角色反应语音/动画配音，`animation` 是合理值：

```cpp
pa_proplist *pl = pa_proplist_new();
pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "animation");
pa_proplist_sets(pl, PA_PROP_APPLICATION_NAME, "NekoMote Wallpaper");
```

不要假定 `event` 一定被静音，但桌面环境/用户策略可能对 event stream 做不同音量处理。因此语音反应更适合 `animation`，而不是伪装成系统 event。

---

## 29. [重要] 锁屏 / 休眠

建议显式设计 audio state：

```text
Running
DesktopHidden/Locked -> Pause/Stop
Suspend            -> Pause + clear pending device timing
Resume             -> reopen/resync if needed
```

不要依赖 PulseAudio 自动帮你维护 NekoMote reaction timeline 同步。

plasmashell 内无法简单用普通应用 `ApplicationInactive` 推导“锁屏”。第一版至少应在 wallpaper item 不再 active/rendering 时暂停音频；后续可接 KDE/session DBus 状态。

---

# C.7 Wayland vs X11

## 30. [重要] 主要差异

渲染层：如果走 Qt Quick Scene Graph，X11/Wayland 差异大部分由 Qt/KWin 隐藏。

输入层：
- X11：可以查询 root/global pointer。
- Wayland：默认只获得 surface-local pointer。
- 不要使用 X11 window ID 逻辑来定位 Wayland surface。

多屏：
- 都应优先使用 Plasma containment/item 自身几何和 Qt screen/window API。
- 不要以为 Wayland 需要插件自己建“每屏 background surface”。

---

## 31. [参考] Wayland 窗口层级

不要把 NekoMote wallpaper 插件实现成 `zwlr_layer_shell_v1` Background layer。

KDE Plasma 的 wallpaper 是 plasmashell 自己的 containment 内容；具体 KWin/Wayland surface role 属于 Plasma shell 的实现，不是 wallpaper plugin contract。

因此此问题对插件层的正确答案是：**不需要也不应该依赖 wlr layer-shell。**

---

## 32. [重要] Plasma 是否自动处理 X11/Wayland 差异

对 QML/Scene Graph/local pointer：大部分自动。

需要你手动抽象的主要是“超出 Qt Quick surface contract 的能力”：

- 全局 pointer query
- X11-specific APIs
- 可能的全局窗口/屏幕坐标假设
- 平台特定性能诊断

---

## 33. [阻塞] 为 Wayland 预留抽象

建议现在就定义：

```cpp
class PointerProvider {
public:
    virtual ~PointerProvider() = default;
    virtual PointerScope scope() const noexcept = 0;
    virtual std::optional<QPointF> positionFor(
        const QQuickItem &item) = 0;
};
```

实现：

```text
X11GlobalPointerProvider   -> GlobalDesktop
LocalPointerProvider       -> LocalSurface
NullPointerProvider        -> Unavailable
```

然后 adapter 再把结果喂给：

```cpp
session.pointer_enter(...)
session.pointer_move(...)
session.pointer_leave()
```

核心引擎完全不知道 X11/Wayland。

---

# C.8 多屏

## 34. [重要] 每屏实例还是共享实例

KDE Plasma scripting 文档明确：**每个 physical screen 有一个 Desktop/containment**。

推荐：

```text
SessionProgram: 可缓存/共享
SessionInstance: 每个 wallpaper item / 每屏一个
GPU resources: 按 QQuickWindow/scene graph 生命周期管理
Audio: 策略决定每屏独立还是只允许当前交互屏播放
```

不建议一个 mutable `SessionInstance` 同时服务两个不同尺寸、不同 DPI、不同 frame cadence 的屏幕。

---

## 35. [重要] 获取多屏几何

优先级：

1. 当前 `QQuickItem::window()` 及其 size/DPR
2. `window()->screen()`
3. `QScreen::geometry()`
4. 需要 app-level topology 时再用 `QGuiApplication::screens()`

监听：

```cpp
QGuiApplication::screenAdded
QGuiApplication::screenRemoved
QScreen::geometryChanged
QScreen::logicalDotsPerInchChanged
QWindow::screenChanged
```

但 wallpaper 本身由 Plasma 重新实例化/移动的行为也要容忍；不要自己保存一个永久 screen index。

---

## 36. [重要] 不同分辨率/DPI

逻辑坐标与物理 framebuffer 必须分开。

建议 NekoMote `set_origin/set_scale` 的语义明确使用“输出物理 pixel”还是“Qt logical pixel”。你现有接口定义写的是输出像素，最安全的是 adapter 统一转为 physical pixel：

```cpp
physicalSize = logicalItemSize * windowDpr;
```

角色相对屏幕尺寸可以定义：

```cpp
scale = userScale * physicalHeight / referenceHeight;
```

不要使用 `QScreen::physicalDotsPerInch()` 来做 UI 缩放；Qt 文档指出 UI 应基于 logical DPI/DPR。

---

## 37. [重要] 多屏鼠标映射

X11 global query：

```text
global desktop coord
  -> 找到该 wallpaper 所属 screen/containment
  -> 减去 screen origin
  -> logical local
  -> × DPR
  -> centered Neko coordinates
```

Wayland：

```text
直接使用当前 wallpaper surface 的 local pointer event
  -> × DPR
  -> centered Neko coordinates
```

这样两条路径最终汇合到同一个 transform。

---

# C.9 生命周期与资源管理

## 38. [重要] plasmashell 重启恢复

不要把“进程内缓存”当持久状态。

重启后应能仅根据：

- wallpaper KConfig
- model/session files
- 内置默认设置

重建全部 `SessionProgram/SessionInstance/GPU/audio`。

需要持久化的只有用户配置，不要持久化 GL handles、reaction runtime state。

---

## 39. [后续] 用户切换 / 注销

正确 RAII：

```text
Wallpaper item destroyed
 -> stop scheduling update
 -> stop accepting loader result
 -> stop/pause audio
 -> release scene graph resources on correct thread
 -> destroy SessionInstance
 -> release shared program ref
 -> worker shutdown/cancel
```

所有 worker callback 必须通过 weak/QPointer 检查 owner 仍存在。

---

## 40. [重要] GPU context / scene graph 丢失

将 Qt 的 `sceneGraphInvalidated` 当作 GPU cache reset：

```text
invalidate:
  drop QSG textures/material device resources
  mark gpuReady = false

next initialize/render:
  re-upload from CPU decoded textures
```

因此 CPU asset 层最好能够在 GPU resource 丢失后重建 texture，而不必重新读取/重新解析完整 E-mote。

---

## 41. [后续] GPU 资源竞争与显存

壁纸和 KWin/compositor 最终共享 GPU，但并不是共享你的 GL state/object namespace 契约。

实际优化：

- PNG atlas 只上传一次；
- 不为每帧创建 texture/FBO；
- VBO/geometry storage 复用；
- 30 FPS 选项；
- background 静态时不要重画；
- 屏幕不可见时暂停；
- 多屏可共享 CPU `SessionProgram`，GPU texture 能否共享由 Scene Graph resource model 决定，不应强行假设。

---

# C.10 性能与功耗

## 42. [重要] 自适应 FPS / 可见性

建议 policy：

```text
Visible + AC:
  user target 60/30

Visible + Battery:
  min(user target, 30)

Not rendered / item invisible:
  0 FPS, pause advance or use coarse catch-up policy

Screen locked:
  0 FPS + audio pause

Configuration preview:
  可单独 30 FPS
```

Qt `QQuickItem::update()` 本身不会保证不可见 item 仍持续 render，因此不要额外制造 busy loop。

如果恢复后不希望动画瞬间跳过数小时：

```cpp
dt = std::min(realDt, 0.1);
```

或在 suspend/resume 时显式 reset simulation clock。

---

## 43. [重要] 电池检测

UPower 官方 DBus：

```text
bus:      system
service:  org.freedesktop.UPower
path:     /org/freedesktop/UPower
interface:org.freedesktop.UPower
property: OnBattery
```

Qt DBus 示例：

```cpp
QDBusInterface upower(
    "org.freedesktop.UPower",
    "/org/freedesktop/UPower",
    "org.freedesktop.UPower",
    QDBusConnection::systemBus());

QVariant onBattery = upower.property("OnBattery");
```

监听 DBus PropertiesChanged，而不是每帧轮询。

---

## 44. [重要] CPU/GPU 预算

现有 NekoMote 425–768 μs/frame：

- 60 FPS 的一帧预算约 16.7 ms；
- 这意味着纯 CPU 求值约占单核 frame budget 的 2.5–4.6%。

从数量级上看非常充裕。

真正要测的是加入 Plasma 后：

```text
advance/prepare
+ CPU -> scene graph copy
+ texture/mask rendering
+ draw calls
+ compositing
+ multi-screen
```

目标建议：

- CPU adapter + core 稳态最好 < 1.5–2 ms/frame
- 不产生持续 heap churn
- 不持续唤醒 60 Hz 当桌面不可见
- GPU 不额外做 full-screen 多次离屏 pass，除非 mask 确实需要

---

## 45. [参考] 性能测量

工具组合：

```bash
QSG_INFO=1
QT_LOGGING_RULES="qt.scenegraph.*=true"
```

Qt Scene Graph timing 的具体 logging category/环境变量随版本有变化，先以当前 Qt 6.9 的运行输出为准，不要硬编码旧 Qt 5 教程。

系统：

```bash
perf record -g -p $(pidof plasmashell)
perf report
```

GUI：

- Hotspot
- KSysGuard/System Monitor
- `nvidia-smi dmon`
- `nvidia-smi pmon`

NVIDIA 下注意 `nvidia-smi` 的 GPU utilization 是设备级采样，不等于 NekoMote 自身准确 GPU time。

若要精确 GPU pass time，用 GL timer query 或对应 RHI timestamp（若 backend 可用）。

---

# C.11 打包与安装

## 46. [阻塞] CMake

顶层示意：

```cmake
cmake_minimum_required(VERSION 3.25)
project(nekomote-plasma-wallpaper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    Qml
    DBus
)

find_package(Plasma REQUIRED)

add_subdirectory(src)

plasma_install_package(
    package
    uk.nekopara.nekomote
    wallpapers
    wallpaper
)
```

src：

```cmake
qt_add_qml_module(nekomote_wallpaper_qml
    URI NekoMote.Wallpaper
    VERSION 1.0
    SOURCES
        nekowallpaperitem.cpp
        nekowallpaperitem.h
        nekorendernode.cpp
        nekorendernode.h
)

target_link_libraries(nekomote_wallpaper_qml PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::DBus
    nekomote
)
```

如果 OpenGL-specific path 仍保留，再加：

```cmake
find_package(Qt6 REQUIRED COMPONENTS OpenGL)
target_link_libraries(... PRIVATE Qt6::OpenGL)
```

---

## 47. [阻塞] 安装路径

系统：

```text
/usr/share/plasma/wallpapers/uk.nekopara.nekomote/
```

用户：

```text
~/.local/share/plasma/wallpapers/uk.nekopara.nekomote/
```

QML C++ module 的 `.so/qmldir` 则进入 Qt/KDE 的 QML import path，不应塞进 wallpaper package 后再用绝对路径 dlopen。

---

## 48. [后续] Arch PKGBUILD

依赖名称要以 Arch 当前仓库拆包为准，不要只根据 upstream CMake target 猜包名。

典型方向：

```bash
depends=(
  'plasma-workspace'
  'qt6-base'
  'qt6-declarative'
  'libpulse'
)
makedepends=(
  'cmake'
  'ninja'
  'extra-cmake-modules'
)
```

如果代码直接依赖 libplasma 开发 CMake config，确认 Arch 对应 package 是否已由 `plasma-workspace`/相关包提供。

PKGBUILD 应使用：

```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=None \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
DESTDIR="$pkgdir" cmake --install build
```

---

## 49. [重要] NekoMote 库一起分发

### 第一版推荐：静态链接到 QML plugin

优点：

- 不处理 SONAME/RPATH；
- 不污染 `/usr/lib`;
- 版本绑定明确。

条件：NekoMote license 与最终插件分发兼容。

### 共享库

适用于多个应用共同使用核心库：

```text
/usr/lib/libnekomote.so.X
```

使用正常 SONAME + linker search path，不要让用户设置 `LD_LIBRARY_PATH`。

不推荐把共享库藏在 wallpaper package 内再加奇怪 RPATH。

---

## 50. [重要] 模型素材分发

技术上可以内置，但版权是独立问题。

建议产品结构：

```text
plugin package:
  engine + UI + 一个明确可再分发的 demo model

user models:
  ~/.local/share/nekomote/models/...
```

每个模型 manifest 保存：

```text
source
author
license
redistribution permission
asset hash/version
```

不要把“已解包的商业 E-mote 素材能在本机运行”误认为“可以随 AUR/GitHub release 再分发”。

---

# C.12 调试与诊断

## 51. [重要] 调试 plasmashell

日志：

```bash
QT_LOGGING_RULES="qt.scenegraph.*=true;org.kde.plasma.*=true" plasmashell --replace
```

在现有 session 中用 `plasmashell --replace` 需谨慎，开发时最好准备 TTY/另一个终端以便恢复。

GDB：

```bash
gdb -p "$(pidof plasmashell)"
```

或独立启动 debug session：

```bash
gdb --args plasmashell --replace
```

推荐为 NekoMote 自己定义 `QLoggingCategory`，例如：

```cpp
Q_LOGGING_CATEGORY(logNekoWallpaper, "nekomote.wallpaper")
```

这样可以：

```bash
QT_LOGGING_RULES="nekomote.wallpaper.debug=true"
```

---

## 52. [重要] 日志查看

systemd user session 常见：

```bash
journalctl --user -f | grep -i plasmashell
```

或：

```bash
journalctl --user-unit plasma-plasmashell.service -f
```

unit 名称依发行版/Plasma session packaging 可能不同，可先：

```bash
systemctl --user | grep plasma
```

`~/.xsession-errors` 在现代 systemd 桌面上不应作为唯一日志来源。

---

## 53. [重要] 独立测试 QML

不要把完整 `WallpaperItem` 直接拿 `qmlscene` 当普通 root 测试，因为 WallpaperItem 依赖 Plasma containment context。

更好的做法：

1. `NekoWallpaperItem` 做成普通 QQuickItem；
2. 写一个独立 harness：

```qml
import QtQuick
import QtQuick.Window
import NekoMote.Wallpaper

Window {
    width: 1280
    height: 720
    visible: true

    NekoWallpaperItem {
        anchors.fill: parent
        modelPath: ...
    }
}
```

这样可用 `qml6`/自建小 executable 测 renderer，而真正 `WallpaperItem` 只负责 glue/config。

这也是减少调试时反复炸 plasmashell 的关键。

---

## 54. [参考] profiling

建议拆三层测：

1. core：
   ```text
   advance
   prepare_frame
   ```
2. adapter：
   ```text
   RenderFrame -> node/geometry upload
   ```
3. end-to-end：
   ```text
   plasmashell + KWin GPU/CPU
   ```

`perf + Hotspot` 对 C++ CPU 最有价值。  
Qt Scene Graph logs 用于 frame/render loop。  
GPU 时间需要 API timer query 或 profiler，不能只看 nvidia-smi 的百分比。

---

# C.13 参考实现

## 55. [阻塞] KDE 官方 wallpaper 源码

最值得读：

### Image / Slideshow

```text
plasma-workspace/wallpapers/image/
```

- https://github.com/KDE/plasma-workspace/tree/master/wallpapers/image
- main QML:
  https://github.com/KDE/plasma-workspace/blob/master/wallpapers/image/imagepackage/contents/ui/main.qml

### KDE Plasma Addons wallpapers

```text
kdeplasma-addons/wallpapers/
```

- https://github.com/KDE/kdeplasma-addons/tree/master/wallpapers
- Haenau:
  https://github.com/KDE/kdeplasma-addons/blob/master/wallpapers/haenau/contents/ui/main.qml
- Hunyango:
  https://github.com/KDE/kdeplasma-addons/blob/master/wallpapers/hunyango/contents/ui/main.qml

这些用于确认 **Plasma wallpaper packaging / WallpaperItem / config**。

渲染层则主要看 Qt 官方 Scene Graph examples。

---

## 56. [参考] 第三方动态壁纸

第三方视频 wallpaper 可参考：

- `plasma-wallpaper-videos` / 类似项目的包结构和生命周期。

但不要从第三方项目反推 Plasma 6 的长期 API 契约。很多项目从 Plasma 5 迁移而来，可能保留 FBO/OpenGL-only/旧 metadata 做法。

第三方项目主要用于回答：
- 播放暂停策略
- config UX
- 多屏体验
- package distribution

不要直接复制它们的 renderer。

---

## 57. [参考] QQuickFramebufferObject 示例

Qt 官方：

https://doc.qt.io/qt-6/qquickframebufferobject.html

结论比示例本身更重要：

> Qt 6 中 QQuickFramebufferObject 仅在 Qt Quick 使用 OpenGL renderer 时工作，并被视为 legacy。

所以 NekoMote 可以做一个 FBO prototype 验证已有 OpenGL renderer，但**不要把它作为长期 Plasma 6 架构基线**。

更值得参考：

- Custom Geometry  
  https://doc.qt.io/qt-6/qtquick-scenegraph-customgeometry-example.html
- Custom QSGRenderNode  
  https://doc.qt.io/qt-6/qtquick-scenegraph-customrendernode-example.html
- RHI Under QML  
  https://doc.qt.io/qt-6/qtquick-scenegraph-rhiunderqml-example.html
- Qt Quick Scene Graph overview  
  https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html

---

## 58. [阻塞] 最小 Hello World

### package/metadata.json

```json
{
  "KPlugin": {
    "Id": "uk.nekopara.nekomote",
    "Name": "NekoMote",
    "Description": "NekoMote animated wallpaper",
    "Version": "0.1.0",
    "License": "GPL-3.0-or-later"
  },
  "KPackageStructure": "Plasma/Wallpaper"
}
```

### package/contents/ui/main.qml

```qml
import QtQuick
import org.kde.plasma.plasmoid
import NekoMote.Wallpaper

WallpaperItem {
    id: root

    Rectangle {
        anchors.fill: parent
        color: "#20242b"
    }

    NekoWallpaperItem {
        anchors.fill: parent
    }
}
```

### src/nekowallpaperitem.h

```cpp
#pragma once

#include <QQuickItem>
#include <qqmlintegration.h>

class NekoWallpaperItem final : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit NekoWallpaperItem(QQuickItem *parent = nullptr)
        : QQuickItem(parent)
    {
        setFlag(ItemHasContents, true);
    }

protected:
    QSGNode *updatePaintNode(
        QSGNode *oldNode,
        UpdatePaintNodeData *) override;
};
```

### src/nekowallpaperitem.cpp

```cpp
#include "nekowallpaperitem.h"

#include <QSGSimpleRectNode>

QSGNode *NekoWallpaperItem::updatePaintNode(
    QSGNode *oldNode,
    UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGSimpleRectNode *>(oldNode);

    if (!node)
        node = new QSGSimpleRectNode;

    node->setRect(boundingRect());
    node->setColor(QColor(255, 80, 160));
    return node;
}
```

### src/CMakeLists.txt

```cmake
qt_add_qml_module(nekomote_wallpaper_qml
    URI NekoMote.Wallpaper
    VERSION 1.0
    SOURCES
        nekowallpaperitem.cpp
        nekowallpaperitem.h
)

target_link_libraries(nekomote_wallpaper_qml PRIVATE
    Qt6::Quick
    Qt6::Qml
)
```

### top-level CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.25)
project(nekomote-wallpaper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)

find_package(Qt6 REQUIRED COMPONENTS Quick Qml)
find_package(Plasma REQUIRED)

add_subdirectory(src)

plasma_install_package(
    package
    uk.nekopara.nekomote
    wallpapers
    wallpaper
)
```

安装后确认：

```bash
kpackagetool6 --type Plasma/Wallpaper --list
```

---

# C.14 NekoMote 特定接入

## 59. [阻塞] 三步管线映射 Qt Quick render loop

### 推荐模型：在 `updatePaintNode()` 同步阶段推进

第一版最清晰：

```cpp
QSGNode *NekoWallpaperItem::updatePaintNode(...)
{
    RenderState &r = node->state();

    const auto now = steady_clock::now();
    const double dt = ...

    r.consumePendingInput();

    if (!r.sessionStarted)
        r.instance->start();

    r.instance->advance(dt);
    r.instance->prepare_frame();

    const RenderFrame frame = r.instance->frame();

    r.updateSceneGraph(frame);
    scheduleNextFrame();

    return node;
}
```

这里满足 NekoMote 的同线程约束。

### 不建议的错误映射

不要做：

```text
GUI QTimer:
  advance()

render thread:
  prepare_frame()
  frame()
```

这是直接违反接口约束。

### `beforeFrameBegin` / `beforeRenderPassRecording` 何时用？

如果最终采用 `QSGRenderNode + native commands`：

- `beforeSynchronizing` / `updatePaintNode`：同步 CPU state
- `beforeRendering`：准备 renderer resources
- `beforeRenderPassRecording`：在 main render pass 内记录 raw commands
- `afterRenderPassRecording`：必要的 post work

但 NekoMote 的 `advance/prepare_frame` 并不必须绑到 `beforeRenderPassRecording`。把 CPU animation evaluation 放到 render-pass recording 才做通常太晚且让职责混乱。

---

## 60. [阻塞] RenderFrame 高效上传与 masks

建议保留一个 persistent renderer cache：

```cpp
struct GeometryCache {
    std::vector<QSGGeometryNode*> drawNodes;
    // 或一个 QSGRenderNode 内的 dynamic GPU buffers
};
```

### GeometryNode 路径

每帧：

```text
vertices: memcpy / update vertex data
indices:  memcpy / update index data
commands:
  node material/texture/blend
  draw range
```

如果 command count 稳定，可以复用 node，不要每帧 delete/new。

### 动态 VBO 策略

无论 QSG 还是 raw GL：

- capacity 只在不足时 grow；
- 每帧只更新 used range；
- 不 shrink；
- index buffer 同理；
- atlas texture 不动。

raw OpenGL 时可采用 orphaning/persistent mapping，但只有 profile 证明 upload 是瓶颈后再做。

### mask

优先判断 E-mote mask 是否能用 stencil 表达：

```text
mask commands -> write stencil
target draw    -> stencil test
```

若 mask 需要 alpha feather/多层组合，则使用 mask texture/FBO 更合适。

注意 Qt Quick main render target 的 stencil availability 不应盲目假设。若 renderer 强依赖自己的 stencil/depth，`QSGRenderNode` + 自有 offscreen attachment 或 RHI texture pass 更可控。

---

## 61. [重要] SessionProgram immutable 与模型切换

推荐 immutable program shared_ptr：

```cpp
using ProgramPtr =
    std::shared_ptr<const nekomote::application::SessionProgram>;

struct PendingModel {
    ProgramPtr program;
    CpuTextureAssets textures;
};
```

worker：

```text
load_asset
load_session_asset
compile_session
decode PNG
```

render sync：

```text
if pending ready:
    create SessionInstance(*pending.program)
    start
    create GPU textures
    prepare first frame
    if OK:
        swap current/pending
```

旧实例继续画到新实例至少能 produce first frame，避免黑屏。

错误时保留旧模型并通过 Q_PROPERTY/Signal 报错，不要把当前桌面清空。

---

## 62. [阻塞] gaze 的 X11 / Wayland 策略

明确产品行为：

```text
X11:
  Full gaze = X11 global pointer available
  即使 pointer 不在 wallpaper item 上，也可持续注视

Wayland:
  Default gaze = LocalSurface
  pointer enter -> enable
  motion        -> update
  pointer leave -> pointer_leave
```

不要承诺 Wayland 下“鼠标在任何应用窗口上角色仍跟随”，除非未来 KDE 提供明确、授权的 compositor API。

因此 UI 可以显示：

```text
Gaze capability:
  Full desktop (X11)
  Desktop-surface only (Wayland)
```

而不是让功能看起来随机失效。

---

## 63. [重要] PulseAudio 权限

用户会话模式下无特殊壁纸沙箱。已有 worker 可复用。

但要修改：

1. 不依赖 GLFW window lifecycle；
2. stream 名称明确；
3. role=`animation`；
4. wallpaper invisible/locked/suspend 时 pause/stop；
5. destructor 不阻塞 render thread 长时间等待音频服务器；
6. `pop_audio_command()` 仍由 session owner thread 读取，然后发送给 audio worker；
7. RMS 用 lock-free snapshot/atomic small value 回写，不要让 PA callback 锁 render mutex。

---

## 64. [阻塞] GLFW preview renderer 能否复用

### 可以复用“渲染算法”，不应直接复用“GLFW 宿主层”。

建议重构：

```text
Before:
nekomote_player
  ├── GLFW window
  ├── OpenGL resource management
  ├── RenderFrame interpreter
  ├── pointer
  └── audio

After:
libnekomote_renderer_common
  ├── render command validation
  ├── blend mapping
  ├── mask planning
  └── texture metadata

nekomote_player
  └── OpenGLRenderer

plasma adapter
  ├── QSGRenderer   (preferred)
  └── OpenGLRenderNodeRenderer (optional)
```

可以复用：

- `DrawCommand` 顺序
- blend equation 映射
- mask pass 逻辑
- texture upload format decisions
- shader mathematical logic
- frame validation

不要直接复用：

- GLFW context creation
- glfw cursor coordinates
- swapBuffers
- framebuffer size callback
- GLFW event loop
- “当前 thread 永远拥有 GL context”的假定

这是 P8 接入最值得先做的一次重构。

---

# 65. 推荐实施顺序

## P8.0：先做最小 Plasma 壁纸

完成：

```text
metadata.json
WallpaperItem
NekoWallpaperItem
QSGSimpleRectNode
KPackage install
```

验收：能在 Wallpaper Type 中选择 NekoMote，plasmashell 不崩。

## P8.1：把 RenderFrame 接到 QSG

先不做：

```text
mask
audio
global pointer
hot reload
```

只渲染无 mask/基础 blend 的模型，验证 geometry、UV、DPR、draw order。

## P8.2：纹理与 blend

建立：

```text
TextureId -> QSG texture
Over/Add/Multiply/Screen
```

对照 GLFW preview 做截图像素比较。

## P8.3：mask

逐模型审计 mask correctness。

## P8.4：输入

先 `LocalSurface`，即使当前环境 X11 也先验证这个统一路径；然后再加 `X11GlobalPointerProvider`。

## P8.5：音频

复用 worker，接 pause/resume policy。

## P8.6：模型热切换

worker compile + frame-boundary swap。

## P8.7：多屏 / DPI

每屏独立 instance，共享 immutable assets。

## P8.8：功耗策略

UPower + visibility + 30/60 FPS。

## P8.9：Wayland 验证

要求不依赖 X11-specific code 即可启动、渲染、local gaze/touch。

---

# 66. 必须避免的实现

Agent 在编码时应把下面这些视为 architecture red flags：

```text
[禁止] 在 GUI thread advance()，render thread prepare_frame()
[禁止] 缓存 RenderFrame.span 到下一帧
[禁止] 在 QQuickItem 析构函数里无 current GL context 直接 glDelete*
[禁止] 每帧 new/delete 全部 draw nodes / textures / FBO
[禁止] 将 GLuint 放进 SessionProgram
[禁止] 在 wallpaper plugin 中强行改变整个 plasmashell 的 QSG graphics backend
[禁止] 把 QQuickFramebufferObject 作为唯一长期方案
[禁止] Wayland 下尝试用 QCursor::pos() 当作可靠全局指针契约
[禁止] 让 model load / JSON parse / PNG decode 阻塞 render thread
[禁止] model switch 先销毁旧实例，再尝试加载新实例
[禁止] 认为每个 screen 的 DPI/尺寸相同
[禁止] 用 screen refreshRate + QTimer 当精准 vsync
[禁止] 把 wallpaper 当作独立 layer-shell app
[禁止] 使用 LD_LIBRARY_PATH 解决正式打包依赖
```

---

# 67. 建议增加到 NekoMote host API 的接口

现有接口已经很适合 adapter，但为了 Plasma / 多后端，建议增加 host-side helper，而不是改 core semantics：

```cpp
struct OutputMetrics {
    int physical_width;
    int physical_height;
    float device_pixel_ratio;
};

struct PointerSample {
    PointerScope scope;
    bool inside;
    Vec2 position;
};
```

以及 renderer-independent 的 texture metadata：

```cpp
struct TextureSource {
    TextureId id;
    int width;
    int height;
    PixelFormat format;
    std::span<const std::byte> pixels;
};
```

如果当前 asset API 还没有公开“TextureId 对应原始 texture source”的稳定接口，**这是 Plasma adapter 开工前真正需要确认/补齐的阻塞项之一**。因为 `RenderFrame` 只有 TextureId，而宿主必须有确定途径取得该 ID 的像素内容才能上传 GPU。

---

# 68. 建议的线程图

```text
                   ┌────────────────────────┐
                   │       GUI thread       │
                   │ QML / input / config   │
                   └───────────┬────────────┘
                               │ snapshots / requests
                               ▼
                   ┌────────────────────────┐
                   │ Qt Quick render thread │
                   │                        │
                   │ SessionInstance owner  │
                   │ advance                │
                   │ prepare_frame          │
                   │ frame                  │
                   │ QSG nodes/render       │
                   └───────┬─────────┬──────┘
                           │         │ AudioCommand
             Program swap  │         ▼
                           │  ┌────────────────────┐
                           │  │ PulseAudio worker  │
                           │  │ play + RMS         │
                           │  └─────────┬──────────┘
                           │            │ atomic RMS
                           │            └─────────────► render thread
                           │
                 ┌─────────▼──────────┐
                 │ loader worker      │
                 │ I/O + parse        │
                 │ compile_session    │
                 │ PNG decode         │
                 └────────────────────┘
```

核心原则：

- `SessionInstance` 单 owner。
- `SessionProgram` immutable，可共享。
- loader 不创建 scene graph/OpenGL resource。
- audio callback 不锁 render thread。
- GUI 不直接修改 SessionInstance。

---

# 69. 参考资料索引

## KDE / Plasma

1. Plasma themes and plugins  
   https://develop.kde.org/docs/plasma/

2. WallpaperItem QML API  
   https://api.kde.org/qml-org-kde-plasma-plasmoid-wallpaperitem.html

3. `WallpaperItem` C++ implementation  
   https://api.kde.org/legacy/plasma/libplasma/html/wallpaperitem_8cpp_source.html

4. Plasma scripting API：每屏 Desktop / wallpaper config  
   https://develop.kde.org/docs/plasma/scripting/api/

5. Plasma configuration tutorial（用于 KConfig 基础；wallpaper 的 `config.qml` 位置应以官方 wallpaper 源码为准）  
   https://develop.kde.org/docs/plasma/widget/configuration/

5a. KDE POTD wallpaper config.qml（Plasma 6 wallpaper 配置布局的直接参考）  
   https://github.com/KDE/kdeplasma-addons/blob/master/wallpapers/potd/package/contents/ui/config.qml

6. KDE Image wallpaper source  
   https://github.com/KDE/plasma-workspace/tree/master/wallpapers/image

7. Image wallpaper main.qml  
   https://github.com/KDE/plasma-workspace/blob/master/wallpapers/image/imagepackage/contents/ui/main.qml

8. Image wallpaper CMake  
   https://github.com/KDE/plasma-workspace/blob/master/wallpapers/image/CMakeLists.txt

9. KDE Plasma Addons wallpaper source  
   https://github.com/KDE/kdeplasma-addons/tree/master/wallpapers

10. libplasma PlasmaMacros / `plasma_install_package`  
    https://github.com/KDE/libplasma/blob/master/PlasmaMacros.cmake

## Qt Quick / Scene Graph

11. QQuickItem  
    https://doc.qt.io/qt-6/qquickitem.html

12. Qt Quick Scene Graph  
    https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html

13. Qt Quick Scene Graph Default Renderer  
    https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph-renderer.html

14. Custom Geometry example  
    https://doc.qt.io/qt-6/qtquick-scenegraph-customgeometry-example.html

15. Custom QSGRenderNode example  
    https://doc.qt.io/qt-6/qtquick-scenegraph-customrendernode-example.html

16. RHI Under QML  
    https://doc.qt.io/qt-6/qtquick-scenegraph-rhiunderqml-example.html

17. QQuickWindow  
    https://doc.qt.io/qt-6/qquickwindow.html

18. Qt 6 Quick changes  
    https://doc.qt.io/qt-6/quick-changes-qt6.html

19. QQuickFramebufferObject（注意 legacy/OpenGL-only 警告）  
    https://doc.qt.io/qt-6/qquickframebufferobject.html

20. QScreen  
    https://doc.qt.io/qt-6/qscreen.html

## Wayland

21. Wayland protocol/model  
    https://wayland.freedesktop.org/docs/book/Protocol.html

22. Wayland pointer model  
    https://wayland-book.com/seat/pointer.html

关键契约：surface-local input；普通客户端不应依赖全局 surface/pointer 坐标。

## Power / Audio

23. UPower D-Bus API  
    https://upower.freedesktop.org/docs/UPower/

24. PulseAudio application/media properties  
    https://www.freedesktop.org/wiki/Software/PulseAudio/Documentation/Developer/Clients/ApplicationProperties/

---

# 70. 可信度与需要本机确认的事项

以下结论可以视为高可信 API 契约：

- Wallpaper root = `WallpaperItem`
- wallpaper package 路径
- Qt Quick render thread / `updatePaintNode`
- QQuickFramebufferObject OpenGL-only/legacy
- Wayland surface-local pointer / 无标准 global pointer
- UPower `OnBattery`
- PulseAudio `media.role=animation`

以下事项建议 Agent 在目标机 Plasma 6.3.6 实际检查一次：

```bash
kpackagetool6 --list-types | grep -i wallpaper
qmake6 -query
qtpaths6 --query
pkg-config --list-all | grep -Ei 'plasma|kf6'
```

以及：

```bash
grep -R '"KPackageStructure".*Wallpaper' \
  /usr/share/plasma/wallpapers \
  /usr/share 2>/dev/null | head
```

还应检查当前实际 Scene Graph backend：

```bash
QSG_INFO=1 plasmashell --replace
```

因为“当前用户机器是 NVIDIA + X11”并不等于“插件可以把 OpenGL 当作 Plasma 6 永久 ABI”。

---

# 71. 最后给实现 Agent 的决策

**P8 第一版建议目标不是“把 GLFW renderer 塞进 wallpaper”，而是“建立一个 Plasma-native renderer adapter”。**

最终边界应当是：

```text
NekoMote core
    ↓ RenderFrame
Renderer interface
    ├── GLFW/OpenGL backend       # 现有 preview
    └── Qt Quick SceneGraph backend # Plasma
```

这能保留核心引擎的“GPU-agnostic host rendering”设计，也避免让 Plasma 6 的 RHI/OpenGL backend 选择反向污染 NekoMote runtime。

如果开发资源有限，短期 OpenGL-only prototype 可以做，但应明确标记为验证阶段，并确保 architecture 不把 `GLuint/QOpenGLContext/QQuickFramebufferObject` 写进 core 层。
