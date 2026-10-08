# G16：可加载的隔离 Plasma 壁纸包

> Agent: CodeArts / Model: GLM-5.2
> Context: docs/agent-workboard.md G16
> 日期: 2026-10-08
> 基线: G15 Plasma 原型（commit 前），C7 冻结证据 `build/evidence/c7-qt-quick-2026-10-08/`

## 1. 任务目标

把 G15 的 Plasma 壁纸原型补成可加载的隔离包，满足：

- probe 加载实际壁纸 `main.qml`（不直接实例化 C++ 类型）
- 组件内置持续播放调度（不依赖 probe 外置 timer）
- RHI 后端检测：非 OpenGL 后端报告 `UnsupportedBackend`（status=3）
- 加载/渲染失败有可见错误（status + errorString）
- 四安全负例保留
- 三个 WebGL 初态/DPI 图像门槛与 C7 不变
- plasma Release/WERROR 构建通过
- 未执行实际桌面安装验收（明确记录）

## 2. 改动文件

| 文件 | 改动 |
| --- | --- |
| `adapters/plasma/wallpaper_item.hpp` | +7 行：`QTimer* timer_`、`bool backend_checked_`、`itemChange()` override、`check_backend()`/`start_timer()`/`stop_timer()` |
| `adapters/plasma/wallpaper_item.cpp` | +46 行：构造函数创建 33ms QTimer；`setPaused`/`set_error` 控制 timer；`componentComplete` 调 `try_load`；`itemChange` 检测 ItemSceneChange 触发 `check_backend`；`check_backend` 检查 `sceneGraphBackend()` + `graphicsApi()` |
| `adapters/plasma/probe/main.cpp` | 重写：加载 `main.qml`（`beginCreate`/`completeCreate` 三步法设置属性）；`--graphics-api opengl|software`；`--max-steps`；通过 `findChild` + `qobject_cast` 获取 NekoWallpaperItem；移除外置 timer |
| `adapters/plasma/CMakeLists.txt` | +4 行：`NEKOMOTE_WALLPAPER_QML` 和 `NEKOMOTE_CONFIG_QML` 编译定义 |
| `adapters/plasma/package/contents/ui/main.qml` | +3 行：`readonly property int status`、`readonly property string errorString`、`objectName: "wallpaper"` |

总计 5 文件，+136 −48 行。

## 3. 包布局

### 3.1 源码包

```
adapters/plasma/package/
├── metadata.json                    # KPackageStructure: Plasma/Wallpaper
└── contents/
    ├── ui/main.qml                  # 壁纸主 QML（Item wrapper + NekoWallpaperItem）
    └── config/main.qml              # 配置 UI
```

### 3.2 构建输出

```
build/plasma/
├── bin/nekomote_plasma_probe        # 隔离验证宿主
├── adapters/plasma/
│   ├── NekoMote/Wallpaper/          # QML 模块 URI 安装目录
│   │   ├── libnekomote_plasma_qmlplugin.a
│   │   ├── qmldir
│   │   ├── nekomote_plasma_qml.qmltypes
│   │   └── package/contents/        # KPackage 内容（符号链接/复制）
│   │       ├── ui/main.qml
│   │       └── config/main.qml
│   └── ...
```

QML 模块 URI: `NekoMote.Wallpaper` 1.0

### 3.3 构建命令

```bash
cmake --preset plasma          # Release + WERROR + LTO + Qt Quick + Plasma
cmake --build build/plasma
ctest --test-dir build/plasma  # 147 tests
```

## 4. 组件设计

### 4.1 NekoWallpaperItem 内置调度

- **timer_**：33ms QTimer（~30fps），父对象为 item 自身，自动随 item 销毁
- **启动条件**：`status_ == 1 && !paused_ && backend_checked_ && OpenGL`
- **停止条件**：paused / error / non-OpenGL 后端
- **itemChange**：检测 `ItemSceneChange`，首次进入场景时调用 `check_backend()`

### 4.2 RHI 后端检测

`check_backend()` 同时检查两个 API：

```cpp
const auto backend = QQuickWindow::sceneGraphBackend();
const auto api = window->graphicsApi();
if ((!backend.isEmpty() && backend != "opengl") ||
    api != QSGRendererInterface::OpenGL)
{
    set_status(3);  // UnsupportedBackend
    stop_timer();
    return;
}
```

原因：Qt 6.9 中 `QQuickWindow::setGraphicsApi(Software)` 不使 `graphicsApi()` 返回 Software（仍返回 OpenGL）。需要 `QQuickWindow::setSceneGraphBackend("software")` 才能使 `sceneGraphBackend()` 返回 "software"。因此同时检查两者。

### 4.3 probe 加载 main.qml

使用 `QQmlComponent::beginCreate` / `completeCreate` 三步法：

1. `beginCreate(engine.rootContext())` — 创建对象但不触发 `componentComplete`
2. `setProperty("modelFile", ...)` 等 — 在 componentComplete 前设置属性
3. `completeCreate()` — 触发 `componentComplete`，此时 `try_load()` 能看到所有属性

通过 `findChild<QObject*>("wallpaper")` + `qobject_cast` 找到 NekoWallpaperItem（QML 中设置了 `objectName: "wallpaper"`）。

## 5. 验证结果

### 5.1 C7 图像门槛（四个截图 SHA256 完全匹配）

| 测试 | 参数 | SHA256 | C7 参考 | 结果 |
| --- | --- | --- | --- | --- |
| Chocola default | origin-x=0, y=400, scale=1, steps=0, 800×1080 | `fe67c87c…` | `fe67c87c…` | ✓ |
| Fraise default | origin-x=0, y=400, scale=1, steps=0, 800×1080 | `d88edb2f…` | `d88edb2f…` | ✓ |
| Fraise placed60 | origin-x=80, y=350, scale=0.8, steps=60, 800×1080 | `a8e1f01a…` | `a8e1f01a…` | ✓ |
| Fraise DPI 1.5 | origin-x=80, y=400, scale=1, steps=0, 800×1080, QT_SCALE_FACTOR=1.5 | `42cb48b7…` | `42cb48b7…` | ✓ |

环境: `QSG_RENDER_LOOP=threaded`（与 C7 一致）

### 5.2 Software 后端拒绝

```
$ nekomote_plasma_probe --model ... --session ... --graphics-api software
wallpaper status=3 errorString=
software backend correctly rejected
exit code: 0
```

### 5.3 持续播放

```
$ nekomote_plasma_probe --model ... --session ... --steps 30 --max-steps 60
wallpaper status=1 errorString=
max-steps reached: rendered_frames=60 generation=1
plasma probe rendered_frames=60
plasma probe renderer created=1 destroyed=1
exit code: 0
```

### 5.4 生命周期（resize → pause → hide → release → resume）

```
lifecycle stage=0 size=800x1080 generation=1 delta=0.000000000
lifecycle stage=1 size=640x480 generation=1 delta=0.350222127
lifecycle stage=2 size=800x1080 generation=2 delta=0.000000000
plasma probe rendered_frames=6
plasma probe renderer created=2 destroyed=2
exit code: 0
```

- generation 1→2：场景图重建后新 generation ✓
- delta=0.000000000：恢复首帧不推进隐藏期间时间 ✓
- renderer created=2 destroyed=2：GPU 资源完整释放 ✓

### 5.5 CTest

| 构建预设 | 结果 |
| --- | --- |
| `build/dev` (Debug + WERROR) | 145/145 passed |
| `build/plasma` (Release + WERROR + LTO) | 147/147 passed |

plasma 额外 2 个测试：`nekomote_plasma_item_safety`（四安全负例）、`python_qt_quick_host`

### 5.6 四安全负例

`nekomote_plasma_item_safety` 通过（包含在 plasma CTest 147/147 中）。

## 6. 未执行项

- **实际桌面安装验收**：未执行 `kpackagetool5 --install` 或在 Plasma 桌面实际加载壁纸。probe 是隔离 QML 验证宿主，不修改桌面设置。
- **Plasma 配置 UI 实际交互**：`config/main.qml` 存在但 probe 未测试其交互行为。
- **多实例并发**：未测试同一窗口多个 NekoWallpaperItem 实例。

## 7. 已知限制

- `setModelFile`/`setSessionFile` 在加载后修改会报错（原型限制，非 ABI 契约）
- `originX`/`originY`/`scale` 必须在加载前设置（同上）
- timer 固定 33ms（~30fps），未做帧率自适应
- `check_backend` 在 `ItemSceneChange` 时触发，如果组件在无窗口环境中 complete 则不会检测（probe 总有窗口，Plasma 桌面也有窗口）
