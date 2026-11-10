# Plasma 插件 Wayland 下 CPU 占用翻倍 — 核查报告

**核查日期**：2026-11-10  
**核查基线**：当前 HEAD，参考文档 `plasma-desktop-2026-10-18.md`（D1 实测 Wayland 约 17.7% 单核）

---

## 问题现象

KDE 桌面背景插件（`adapters/plasma/`）在 Wayland 下的 CPU 占用相比 X11 环境几乎翻倍。

---

## 核查结论：多因叠加，主因有三

### 原因 1（最可能的主因）：Wayland 下 Qt 实际渲染频率远高于 X11

**代码位置：** [`adapters/plasma/wallpaper_item.cpp:28-42`](file:///run/media/chocola/MIKU/NekoMote/adapters/plasma/wallpaper_item.cpp#L28-L42)、[`src/host_qt_quick/session_item.cpp:168-367`](file:///run/media/chocola/MIKU/NekoMote/src/host_qt_quick/session_item.cpp#L168-L367)

NekoMote 的 Plasma 插件使用 `QQuickFramebufferObject`（FBO），在 FBO 的 `Renderer::render()` 里调用：
```text
nekomote_wallpaper_advance(elapsed)   ← CPU 物理/动画步进
nekomote_wallpaper_render_frame(...)  ← GPU 绘制
```

渲染频率 = Qt scene graph 的帧频率 = `QQuickWindow` 的渲染频率。

**Wayland 下（KDE plasmashell）：**  
Qt threaded render loop 通过 `wl_surface.frame` 回调驱动，合成器（KWin）以显示器 vblank 节奏通知 plasmashell 可以提交下一帧。D1 测试环境是 **179.999 Hz** 显示器，这意味着 KWin 以 ~180 fps 驱动 plasmashell 渲染。  
→ **NekoMote `advance` + `render_frame` 以约 180 Hz 被调用**

**X11 下（KDE plasmashell / XWayland）：**  
plasmashell 的输出经过 KWin 的 X11 合成层重定向（composited）。在 X11 合成模式下，plasmashell 的 `eglSwapBuffers` 不直接对应物理 vblank，KWin 控制最终呈现频率。  
更重要的是：`wallpaper_item.cpp` 的 `QTimer` 间隔为 17ms（≈60 fps），在 X11 下 Qt scene graph 可能以 timer 频率（60fps）而非显示器频率（180fps）渲染，因为 X11 合成路径不保证 compositor-driven frame pacing。

**结果：Wayland 180fps vs X11 ~60fps → advance/render 调用次数约 3 倍之差，CPU 比例翻倍以上。**

> [!IMPORTANT]
> 这是**架构级**差异，不是 NekoMote 代码的 bug，而是 `QQuickFramebufferObject` 在 Wayland compositor-driven 帧驱动模式下的固有行为。timer 只是保底触发，真正决定帧率的是 `wl_surface.frame` 回调节奏。

---

### 原因 2：fps_limit=0 时 timer 变成零计时器

**代码位置：** [`adapters/plasma/wallpaper_item.cpp:34-40`](file:///run/media/chocola/MIKU/NekoMote/adapters/plasma/wallpaper_item.cpp#L34-L40)

```cpp
const int interval =
    fps_limit == 0
        ? 0   // ← 危险！interval=0 → QTimer 变成"空闲触发"计时器
        : (fps_limit > 0 ? std::max(1, (1000 + fps_limit / 2) / fps_limit) : 17);
```

当配置的 `fps` 被读取为 `0`（表示"不限帧率"）时，`QTimer::setInterval(0)` 会让计时器退化为事件循环空闲触发模式，GUI 线程几乎不停地发 `QQuickItem::update()`，让 render loop 以最高速度运行，CPU 飙升。

> [!WARNING]
> `fps_limit == 0` 映射到 `interval = 0` 的分支在 Wayland 高刷显示器下，会导致插件以显示器全频（180fps）不停 `advance+render`，CPU 占用会远超预期。这个分支理应映射到"以 compositor vblank 驱动，不再加额外 timer"，而不是 `interval=0`。

---

### 原因 3：Wayland 下 Qt 获得 GLES 上下文，X11 下获得 Desktop GL

**代码位置：** 见 [C17 记录](file:///run/media/chocola/MIKU/NekoMote/docs/wayland-compatibility-2026-10-13.md)

Wayland 下 Qt 默认创建 **OpenGL ES** 上下文（GLES 3.2），X11 下获得 **Desktop GL 3.3**。  
GLES 路径存在 **POT padding** 额外开销：
- GLES 3.0 不保证任意尺寸纹理，故 C17 修复中对图集做了 POT 对齐存储
- 202 张图集中 199 张需要 padding，RGBA 容量从 4.49 GiB 增加到 6.26 GiB
- 每帧渲染需要采样更大的纹理区域，同时 GLES driver 路径的 GPU→CPU 同步开销也不同于 desktop GL

这会增加每帧的实际 GPU 时间，造成 CPU 侧的 fence/sync 等待时间增加。

---

### 原因 4（次要）：Qt Quick host 未使用 `nekomote_wallpaper_needs_render` 节流

**代码位置：** [`src/host_qt_quick/session_item.cpp:168`](file:///run/media/chocola/MIKU/NekoMote/src/host_qt_quick/session_item.cpp#L168) (`render()`)

`layer_shell` 和 `x11` 适配器都调用了 `nekomote_wallpaper_needs_render()` 来跳过无变化帧的 present：

```cpp
// adapters/layer_shell/host.cpp:1002
if (!nekomote_wallpaper_needs_render(wallpaper_)) 
    continue;  // 跳过本帧
```

但 **Qt Quick host（Plasma 插件）的 `WallpaperRenderer::render()` 完全没有这个检查**。即使画面没有变化，每次 Qt scene graph 渲染时都会完整执行 `advance + render_frame`。在 Wayland 180fps 下，这意味着每秒约 180 次无效渲染。

---

## 各原因的 CPU 贡献估算

| 原因 | 类型 | 量级 |
|---|---|---|
| Wayland compositor 驱动频率（180Hz vs ~60Hz） | 结构性差异 | ×2～×3 |
| `fps_limit=0` → `interval=0` 零计时器 | 代码缺陷 | 叠加性，可叠加到原因1之上 |
| GLES POT padding 额外 GPU/sync 开销 | 平台差异 | 约 10%～30% 额外 |
| 未使用 `needs_render` 节流 | 优化缺失 | Wayland 下尤其严重 |

---

## 建议的修复方向

### 修复 1（高优先级）：在 Qt host 添加 `needs_render` 检查

在 [`src/host_qt_quick/session_item.cpp`](file:///run/media/chocola/MIKU/NekoMote/src/host_qt_quick/session_item.cpp) 的 `WallpaperRenderer::render()` 中，`advance` 之后检查 `needs_render`；若为 false 且未暂停，可跳过本帧 `render_frame` 和 `render_ui`，仅保留已有 FBO 内容：

```cpp
check(nekomote_wallpaper_advance(...), "advance wallpaper");
// 新增：若无实质变化且非首帧，跳过 GPU 绘制
if (!nekomote_wallpaper_needs_render(state_->wallpaper.get()) && !first) {
    // 保留旧 FBO，Qt scene graph 会复用上一帧纹理
    return;
}
// 以下 render_frame / render_ui 维持原样
```

### 修复 2（高优先级）：修正 `fps_limit=0` 的 timer 间隔

[`adapters/plasma/wallpaper_item.cpp:36`](file:///run/media/chocola/MIKU/NekoMote/adapters/plasma/wallpaper_item.cpp#L34-L40) 中 `fps_limit == 0` 应映射为"由 compositor 驱动，不加额外 timer"，即停止 timer 而非 `setInterval(0)`：

```cpp
// 当前错误：fps_limit == 0 → interval = 0 → 零计时器
// 修复：fps_limit == 0 → 停止 timer，让 compositor frame callback 自然驱动
const int interval =
    fps_limit == 0
        ? -1  // 特殊值：停止 timer
        : (fps_limit > 0 ? std::max(1, (1000 + fps_limit / 2) / fps_limit) : 17);
if (interval < 0) {
    stop_timer();
} else {
    if (timer_->interval() != interval)
        timer_->setInterval(interval);
    // start_timer() 由 refresh_playback 控制
}
```

> [!NOTE]
> 在 Wayland compositor-driven 模式下，停止 timer 后 Qt scene graph 仍会以 compositor vblank 频率渲染（180fps）。要真正限制到 60fps，需要使用 `QQuickWindow::setMinimumRenderInterval()`（Qt 6.x）或等价机制，或通过 `needs_render` 在渲染时跳过无变化帧，让 advance 在 timer 触发时单独执行而渲染复用旧帧。

### 修复 3（中优先级）：强制 Wayland 下也使用 Desktop GL

通过在 plasmashell 插件加载时设置 `QSurfaceFormat` 强制 Desktop GL（而非 GLES），消除 POT padding 开销。**注意：** 这需要合成器支持，且 Wayland 下 Qt 对 Desktop GL 的支持依赖驱动，NVIDIA 在某些配置下 Wayland 只提供 GLES。

---

## 验证建议

1. **帧率验证**：在 `WallpaperRenderer::render()` 中统计调用频率（`state_->rendered_frames`），在 Wayland 180Hz 显示器下观察 1 秒内帧数是否接近 180。
2. **需 `needs_render` 节流前后对比**：加入修复 1 后，在 Wayland 下重新用 `plasma_desktop_measure.py` 测 Plasma Shell CPU。
3. **timer interval 修复验证**：确认 `fps_limit=0` 情况下 timer 是否停止，以及画面是否仍由 compositor 驱动正常刷新。

---

## 相关文件引用

| 文件 | 作用 |
|---|---|
| [`adapters/plasma/wallpaper_item.cpp`](file:///run/media/chocola/MIKU/NekoMote/adapters/plasma/wallpaper_item.cpp) | GUI 线程 timer / update() 驱动 |
| [`src/host_qt_quick/session_item.cpp`](file:///run/media/chocola/MIKU/NekoMote/src/host_qt_quick/session_item.cpp) | render 线程 advance + render_frame 路径 |
| [`adapters/layer_shell/host.cpp:1002`](file:///run/media/chocola/MIKU/NekoMote/adapters/layer_shell/host.cpp#L1000-L1005) | 对照：layer_shell 有 needs_render 节流 |
| [`docs/plasma-desktop-2026-10-18.md`](file:///run/media/chocola/MIKU/NekoMote/docs/plasma-desktop-2026-10-18.md) | 真实 Wayland CPU 测量数据 |
| [`docs/wayland-compatibility-2026-10-13.md`](file:///run/media/chocola/MIKU/NekoMote/docs/wayland-compatibility-2026-10-13.md) | GLES vs Desktop GL 差异说明 |
