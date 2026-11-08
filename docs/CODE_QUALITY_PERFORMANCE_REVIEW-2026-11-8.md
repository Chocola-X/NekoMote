# NekoMote 代码质量与性能优化审查报告

- **审查对象**：`https://github.com/Chocola-X/NekoMote`
- **审查提交**：`c86f469e2f39f53c2c787eab8b1e7dc143a0b956`（`Add English Readme`）
- **审查日期**：2026-11-07
- **目标**：在保持画面与行为正确的前提下，寻找高性价比的 CPU、GPU、内存和显存优化点。

> 本文保留原作者对基线及第一版未提交实现的审查记录。A 阶段后续独立复核发现的问题、
> 已撤回的音频优化及最终修复/测试范围，分别见
> [独立复核](code-quality-performance-audit-2026-11-08.md)和
> [修复验收](code-quality-performance-fixes-2026-11-08.md)。剩余建议的分步基线、取舍及最终保留项见
> [后续优化验收](remaining-optimization-2026-11-08.md)。下列初版实施记录不代表最终状态。

## A 阶段实施记录（2026-11-07）

以下项目已在当前工作区实现；下文后续章节仍保留对基线提交的原始审查，便于对照：

1. `RenderFrame` 新增内容 revision。`GeometryInstance` 仅在成功 `build_frame()` 后递增；GL/Vulkan 对相同非零 revision 跳过重复校验、命令复制和上传，revision 0 始终完整校验，`reserve()` 使门控失效。
2. player、X11、layer-shell 在稳定暂停时跳过 GPU prepare/render/present，并以低频轮询异步候选；输入、resize、输出目标、UI、角色和后台完成会置脏并补画。player 的截图与 `--check-window` 诊断帧保持原节奏。
3. 共享 ABI 与 player 在已知音频设备失败后释放设备/PCM bank，并将后续候选切到 `AudioMode::Disabled`，避免无意义解码。显式取消静音重新允许设备模式和后续加载重试；bank 已释放时，当前角色需重新加载才能恢复声音。
4. `session.cpp` 将绑定发现阶段编译的 `GeometryCompileResult` 移交给 `compile_playback()`，不再重复编译完整 geometry scene。
5. OpenGL 增加与 Vulkan 一致的 `view.alpha` 有限性及 `[0,1]` 校验。

新增/更新测试覆盖 geometry revision、GL/Vulkan 门控和失效、零 revision 校验、GL alpha 及空 ABI 查询。Debug + Werror 全量构建成功；Xvfb + Mesa 软件驱动下 `ctest` 为 **258/258 通过，10 项按既有条件跳过**。Qt 6/KF6 的 `plasma` preset 也已从零配置并完成 **183/183** Release/LTO 构建步骤，包含 player、Qt Quick host 和 Plasma QML 链接。跳过项主要是仓库未包含的真实素材集成及字体环境用例，不能据此宣称真实资产和真实桌面已验收。

本次没有执行同条件发布态性能对照，因此上述改动只说明已消除的重复工作和停止的 GPU 提交，不声称实测 FPS、CPU、内存或显存降幅。真实驱动、复杂模型、桌面暂停交互及声音恢复仍需本地验证。

---

## 1. 结论摘要

当前代码已经具备不少良好的性能基础：模型帧有 CPU 侧 dirty 检查；几何容器和 GPU buffer 会预留并复用；纹理解码后只保留 GPU 资源；索引拓扑和 UV 会跨帧复用；正常帧路径没有 `glFinish` 或像素读回；Plasma 在不可见时会停止定时器。因此，本报告不建议推倒重写，也不建议优先做 SIMD、多线程求值或自定义分配器。

最值得先做的是以下四项：

1. **给 GPU 帧准备增加 revision/dirty 门控**：避免同一模型帧被重复校验、复制和上传，暂停和高刷新率场景都可受益。
2. **独立播放器、X11、layer-shell 在暂停且画面无变化时停止重绘/呈现**：这是最直接的全资源空闲优化；Plasma 已有停止定时器机制，可复用其思路。
3. **音频改为按需加载/解码，避免全部语音 float PCM 常驻**：对语音较多的角色，可能是最显著的常驻内存优化。
4. **为普通角色帧增加直接渲染快路径，并按需分配离屏目标**：减少全屏中间纹理、最终 blit 及其显存/带宽开销。

建议实施顺序：**1 → 2 → 3 → 4**。前两项改动相对局部，第三项收益取决于语音资产规模，第四项收益明确但必须用双后端像素对照保护颜色、遮罩和混合语义。

> 本报告中的“高开销”“可跳过”等判断来自静态调用链和资源尺寸计算，不是实测性能百分比。仓库没有真实角色/背景资产，当前环境也未执行真实桌面 GPU 基准，因此不虚构 FPS、CPU 降幅或显存实测值。

---

## 2. 优先级总表

| 优先级 | 发现 | 主要收益 | 改动成本/风险 | 证据性质 |
|---|---|---|---|---|
| P1 | 未变化帧仍完整执行 GPU `prepare` | CPU、CPU→GPU 带宽；暂停时尤其明显 | 低；需正确失效 revision | 代码可证实 |
| P1 | 独立宿主暂停后仍按 FPS 重绘和呈现 | CPU、GPU、显存带宽、功耗 | 低到中；需保留输入/resize/异步完成唤醒 | 代码可证实；Plasma 不受此问题影响 |
| P1 | 全部语音提前解码为 float PCM 并常驻 | 常驻内存、启动/切换 CPU 和延迟 | 中；需处理首播延迟与线程交接 | 代码可证实 |
| P1 | 所有角色帧使用全屏离屏色目标和最终 blit | GPU、显存、带宽 | 中到高；颜色/混合回归风险 | 代码和资源公式可证实 |
| P2 | OpenGL UI 每个图元单独 `glBufferData` + draw | CPU、驱动调用、buffer orphaning | 中；主要是批处理重构 | 代码可证实 |
| P2 | GLES 图集向 2 的幂填充 | 显存；特定尺寸最高接近 4 倍 | 中；必须保持采样精度 | 代码可证实 |
| P2 | 会话加载时完整编译两次几何场景 | 启动/切换 CPU、瞬时内存 | 低 | 代码可证实 |
| P2 | Vulkan 每个资源独立分配，上传逐次同步等待 | 驱动内存开销、加载 CPU/延迟 | 中；只优化加载路径，不建议改稳态 | 代码可证实 |
| P3 | 音频线程空闲/暂停仍周期轮询 | 空闲 CPU 唤醒 | 中；线程唤醒正确性需谨慎 | 代码可证实 |
| 质量 | GL 未像 Vulkan 一样校验 `view.alpha` | API 一致性、错误诊断 | 低 | 代码可证实 |

---

## 3. 详细发现

### P1-1：CPU 已识别“帧未变化”，GPU 却仍重复完整准备和上传

**结论：代码可证实，且是首选优化项。**

`PlaybackInstance::prepare_frame()` 已经有正确的短路逻辑：帧未 dirty 且已准备好时直接返回。

```cpp
// src/runtime/playback.cpp:359-375
PlaybackStatus PlaybackInstance::prepare_frame() noexcept
{
    auto& data = *impl_;
    ...
    if (!data.dirty && data.frame_ready)
        return {};
    ...
    data.geometry.evaluate();
    ...
    data.geometry.build_frame();
```

但调用方无法区分“复用了旧帧”和“生成了新帧”，随后仍无条件执行 renderer `prepare()`：

```cpp
// src/abi/wallpaper_instance.cpp:675-696
controller_.prepare_frame();
...
renderer_->prepare(controller_.current_instance()->frame());
```

OpenGL 的准备路径每次都会：

- 扫描全部顶点，检查有限数；
- 扫描全部索引，检查范围；
- 扫描全部普通/遮罩 draw command；
- 再次遍历与遮罩有关的索引，计算包围盒；
- 上传整个 VBO/EBO；
- 复制 commands/masks。

```cpp
// src/render_gl/renderer.cpp:512-520
for (const auto& vertex : frame.vertices) { ... }
for (auto index : frame.indices)
    if (index >= frame.vertices.size()) ...

// src/render_gl/renderer.cpp:555-590
const auto bounds = [&frame](const DrawCommand& command)
{
    ...
    for (std::uint32_t i = 0; i < command.index_count; ++i) ...
};
...

// src/render_gl/renderer.cpp:591-600
glBufferSubData(... frame.vertices.size() ...);
glBufferSubData(... frame.indices.size() ...);
p.commands.assign(...);
p.mask_commands.assign(...);
p.masks.assign(...);
```

Vulkan 有同样的重复扫描和完整 `memcpy`：

```cpp
// src/render_vk/renderer.cpp:639-647, 717-727
for (const auto& vertex : frame.vertices) ...
for (auto index : frame.indices) ...
...
std::memcpy(buffer.mapped_data, bytes, size);
...
p.commands.assign(...);
```

这在两种常见场景浪费明显：

1. **暂停**：CPU 帧不变，但独立宿主仍重绘。
2. **显示 FPS 高于 60**：模型 author tick 固定为 60 Hz（`src/runtime/playback.cpp:12`），没有新 tick 的显示帧仍重新上传。

**建议实现**

- 在 `GeometryInstance` 或 `PlaybackInstance` 输出中增加单调递增的 `frame_revision`，仅在 `build_frame()` 成功生成新内容后递增。
- 每个 renderer 保存 `uploaded_revision`；相同 revision 直接跳过 `prepare()`。
- renderer 新建、`reserve()` 重建 buffer、上下文恢复后，必须将本地 revision 设为无效，确保至少上传一次。
- `release_unused_textures()` 的语义不能被 revision 门控破坏；背景淡入淡出结束后的回收仍需执行。
- CPU 求值的 dirty 与 GPU 上传 revision 应分开：前者避免重建几何，后者避免重复传输。
- 对外部 C ABI，调用方可以任意构造新帧，不能仅凭指针相同就跳过。可给内部受信帧增加 revision，外部 ABI 保留完整校验；或者增加显式 revision 契约。

**预期收益**：减少 CPU 扫描、命令复制和重复上传。它不减少真正变化帧的求值成本，也不直接减少稳态显存容量。

**验证**：

- 暂停连续渲染两次，第二次不得调用上传；恢复、拖动、切角色后必须上传。
- 30/60/120 FPS 固定种子对照，确保画面一致。
- OpenGL/Vulkan、mask/no-mask、crossfade、context reset、resize、角色切换均覆盖。
- 用分配计数和上传字节计数验证，而不是只看平均 FPS。

---

### P1-2：独立播放器、X11 和 layer-shell 暂停后仍持续重绘

**结论：代码可证实；Plasma 已采取正确方向，不应把此发现误套到 Plasma。**

模型推进在暂停时会返回：

```cpp
// src/runtime/playback.cpp:316-317
if (data.state == PlaybackState::Paused)
    return {};
```

但普通窗口播放器仍执行 `prepare`、背景/角色/UI 渲染和 `SwapBuffers`，然后仅按 FPS 等待：

```cpp
// apps/player/main.cpp:1238-1248, 1311-1317, 1569-1582
if (w <= 0 || h <= 0 || surface.minimized) { ... continue; }
...
controller.prepare_frame();
gpu.renderer->prepare(...);
...
gpu.renderer->render(...);
...
glfwSwapBuffers(window.get());
...
glfwWaitEventsTimeout(remaining);
```

X11 的主循环只检查 mapped、尺寸和下一帧时间，没有检查用户暂停：

```cpp
// adapters/x11/host.cpp:798-807
if (mapped_ && width_ > 0 && height_ > 0 && Clock::now() >= next_frame_)
    render();
...
poll(&descriptor, 1, timeout);
```

layer-shell 同样在 frame callback 和 FPS 到期后继续 render：

```cpp
// adapters/layer_shell/host.cpp:1128-1129
if (frame_ready_ && Clock::now() >= next_frame_)
    render();
```

对比 Plasma，不可见或暂停时会停止 QTimer：

```cpp
// adapters/plasma/wallpaper_item.cpp:377-393
const bool suspended = paused_ || ... || !host_window_->isExposed();
...
if (suspended)
    stop_timer();
else
    start_timer();
```

**建议实现**

为共享 `WallpaperInstance` 增加明确的 **presentation dirty** 状态，而不是仅查询 `paused()`：

- 用户输入、UI 动画、角色/背景加载完成、crossfade、resize、后端切换、配置变化时置 dirty；
- 正常动画运行时按 FPS 绘制；
- 暂停且没有 dirty 时不再 `advance + prepare + render + present`；
- 进入暂停时允许再画一帧，确保最后的 UI/暂停状态落屏；
- X11/Wayland 进入阻塞事件等待；有新输入、resize 或异步任务完成时唤醒；
- layer-shell 停止请求新的 frame callback，恢复时重新请求；
- Vulkan 恢复前仍需遵守已有 fence/资源生命周期契约。

**预期收益**：暂停静止时避免整套 CPU 准备、GPU 绘制和呈现，是覆盖 CPU/GPU/带宽的空闲优化。不是把 FPS 从 60 改成 30 的折中，而是静止时接近事件驱动。

**验证**：暂停前后像素一致；暂停中拖动 UI、resize、切换成功/失败、后端恢复都能产生新帧；Wayland frame callback 不泄漏、不重复注册。

---

### P1-3：启用声音后，全部语音被提前解码为 float PCM 并常驻

**结论：代码可证实，可能是大语音资产下最大的常驻内存项。**

候选角色加载会遍历 `program.audio_paths()`，把每条 FLAC 全部解码并放入 `audio_clips`：

```cpp
// src/application/controller.cpp:253-263
for (const auto& path : program.audio_paths())
{
    auto clip = asset_io::load_flac(...);
    ...
    result.audio_clips.push_back(std::move(clip.clip));
}
```

音频格式是交错 `float`：

```cpp
// include/nekomote/model/audio_clip.hpp:8-19
struct AudioClip
{
    std::uint32_t sample_rate{0};
    std::uint32_t channels{0};
    std::vector<float> samples;
    std::vector<float> rms;
};
```

FLAC 解码器按完整帧数一次性分配：

```cpp
// src/asset_io/audio.cpp:41-46
state.frames = info.total_samples;
...
state.clip.samples.resize(
    static_cast<std::size_t>(state.frames * info.channels));
```

设备创建时又遍历所有样本做范围检查，解码过程本身还计算了全片 RMS：

```cpp
// src/audio/device.cpp:46-58
for (const auto& value : *clips)
    ... std::any_of(value.samples.begin(), value.samples.end(), ...)
```

此外，`create_audio_device()` 在确认 PulseAudio 可用之前就持有完整 bank；已知 `audio_failed_` 时，候选加载仍先完成全部解码：

```cpp
// src/abi/wallpaper_instance.cpp:414-423
if (candidate.audio_clips.empty())
    return;
audio_clips_ = std::make_shared<const model::AudioBank>(
    std::move(candidate.audio_clips));
if (audio_failed_)
    return;
```

**内存上界公式**（不是资产实测）：

```text
单个 clip PCM = 秒数 × 采样率 × 声道数 × 4 字节
bank 常驻内存 ≈ 全部 clip PCM 之和 + RMS + vector/bank 开销
```

例如，一段 60 秒、48 kHz、双声道语音，仅 PCM 就约为 `60×48000×2×4 = 23,040,000` 字节（约 21.97 MiB）。实际收益必须对目标资产求和后评估。

**建议分两级实施**

1. **低风险第一步**：
   - `audio_failed_` 已知时不再解码或保留 bank；
   - 启动/切换只解析路径与元数据，首次播放前在 worker 解码当前 clip；
   - 只缓存当前 clip，或有界的 1～2 条 LRU；设置全 bank 字节上限，而非只有单 clip 256 MiB 限制。
2. **进一步优化**：
   - 对短语音保留解码缓存，对长语音流式解码；
   - 若 PulseAudio 输出改为 S16，可评估用 `int16_t` 存储，理论上 PCM 容量相对 float 减半，但需处理 24/32-bit 源精度、转换 CPU 和音质；
   - RMS 元数据可在离线导入阶段生成并随资产保存，避免重复全片扫描。

**正确性约束**：不能首次触摸时才在求值线程同步解码，否则会造成卡顿。首播可预取，反应仍由现有有界命令队列交接；口型 RMS 必须与 PulseAudio 播放时钟保持一致。

**验证**：角色切换峰值 RSS、稳定 RSS、首播延迟、音频 underrun、暂停/恢复、设备失败、连续反应和切角色期间的旧 worker 退出。

---

### P1-4：角色帧始终经过全屏离屏色目标；mask target 也按输出尺寸预留

**结论：代码可证实。可以减少显存和全屏带宽，但快路径必须严格限制语义。**

模型帧固定使用 `ProtectedStraight`：

```cpp
// src/runtime/geometry_scene.cpp:1377-1383
return { ..., ColorComposition::ProtectedStraight, ... };
```

因此两个后端都为每次角色渲染分配全输出尺寸的 RGBA8 中间纹理。OpenGL：

```cpp
// src/render_gl/renderer.cpp:635-645
auto color = intermediate(width, height, GL_RGBA8);
auto mask = intermediate(floor_power_two(width),
                         floor_power_two(height), GL_RGB5_A1);
```

绘制结束后再做一次全屏 blit：

```cpp
// src/render_gl/renderer.cpp:794-807
glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
...
glDrawArrays(GL_TRIANGLES, 0, 3);
```

Vulkan 同样分配 color/mask image，并在最终 target pass 中 blit：

```cpp
// src/render_vk/renderer.cpp:766-771, 998-1012
auto color = create_image(... width, height, VK_FORMAT_R8G8B8A8_UNORM ...);
auto mask = create_image(... floor_power_two(width), floor_power_two(height) ...);
...
vkCmdDraw(cmd, 3, 1, 0, 0, 0);
```

**单个 renderer 的理论像素容量**：

```text
color_target = W × H × 4 字节
mask_target  = 2^floor(log2(W)) × 2^floor(log2(H)) × 2 字节
```

以 3840×2160 为例，color 为约 31.64 MiB，mask 为 2048×1024×2，约 4 MiB；不含 swapchain、宿主输出、图集、对齐和驱动额外分配。角色 crossfade 时 current/previous renderer 都存在，离屏目标也会同时存在。

**建议**

1. 在 `prepare()` 时分析整帧是否满足直接绘制条件：
   - `view.alpha == 1`；
   - 没有 `ColorProtection::PreserveTranslucent`；
   - 没有 `BlendMode::Multiply`；
   - 普通 Over 的直接合成经像素对照确认与离屏路径一致。
2. 满足时直接绘制到最终 target，避免 color target 和最终 blit；mask 若仍存在，可独立保留。
3. 仅在确实需要离屏合成时分配 color target，而不是所有角色帧启动即分配。
4. 中期再把 mask target 改为 mask 联合包围盒尺寸，并在采样时加入 offset；当前代码已计算 `scratch_clip`，但 backing store 仍接近全屏。这个改动可进一步降低显存，但 UV/坐标转换和边界过滤必须仔细验证。

**不能直接删除离屏路径的原因**：color protection 依赖角色内部目标 alpha；Multiply 依赖目标颜色；crossfade 的最终 alpha 合成也依赖当前离屏设计。无条件下发 direct draw 很可能改变画面。

**验证**：普通帧、mask、多 mask、protection、Multiply、半透明、crossfade、透明背景、镜像、resize，在 GL/Vulkan 间逐像素比较；显存按 idle/crossfade 分开记录。

---

### P2-5：OpenGL UI 在每个图元上重复分配/上传 buffer 并立即 draw

**结论：代码可证实，面板打开时收益更大。**

每个圆角矩形都向 vector 添加 6 个顶点，随即 flush；flush 对位置和 UV 各调用一次 `glBufferData`：

```cpp
// src/render_gl/ui_renderer.cpp:451-461
rect_vertices_.insert(...);
rect_uvs_.insert(...);
...
flush_rects();

// src/render_gl/ui_renderer.cpp:464-484
glBufferData(GL_ARRAY_BUFFER, ... rect_vertices_ ...);
glBufferData(GL_ARRAY_BUFFER, ... rect_uvs_ ...);
glDrawArrays(...);
rect_vertices_.clear();
rect_uvs_.clear();
```

文本按每次 `draw_text()` 单独重复同样过程（`487-525`），图标也是（`527-567`）。这意味着面板中的每行文字、矩形、图标都产生独立上传和 draw，而非每帧一次或少量批次。即使只有常驻 toggle 图标，也存在固定每帧上传。

Vulkan UI 已采用每帧收集顶点和 draw 的形式（`src/render_vk/ui_renderer.cpp:532-595`），说明共享 UI 并不需要 GL 这种立即 flush 形式。

**建议**

- 采用与 Vulkan 类似的 frame command list：保留 painter 顺序，按 shader/纹理/clip/混合状态划分相邻 batch；
- 预分配并复用 VBO，使用 `glBufferSubData` 或 orphan + subdata 的明确策略，不为每个文字/矩形重设整个 buffer；
- 常驻且未变化的 UI 可复用上一帧顶点；面板关闭时只更新图标；
- 不为了“减少 draw”而跨透明图层重排。

**附带质量观察**：Vulkan UI 初始化固定预留 65536 个顶点、8192 个 draw（`src/render_vk/ui_renderer.cpp:443-448`），每个 UI 实例至少 1 MiB mapped VBO 和较大的 CPU vector 容量。完成批处理统一后，可按真实高水位收缩，避免多实例过度预留。

---

### P2-6：OpenGL ES 图集向 2 的幂扩展，可能显著放大显存

**结论：代码可证实，仅影响使用 `GL_TEXTURE_2D` 的 GLES 路径，桌面 rectangle 路径不受影响。**

```cpp
// src/render_gl/renderer.cpp:472-490
// Power-of-two storage gives exact binary scaling ...
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
             storage_size(image.width), storage_size(image.height),
             ...);
```

`storage_size()` 是向上取 2 的幂。其容量关系为：

```text
显存像素容量 = next_pow2(W) × next_pow2(H) × 4
原始容量     = W × H × 4
```

例如 1025×513 的裁切图集会扩展为 2048×1024，容量约为原始 RGBA 的 3.99 倍。某个维度略超过 2 的幂时浪费最大；也可能因扩展后的尺寸超过 driver limit 而拒绝原可支持的图。

**建议**

- 优先验证 GLES3 非 2 的幂纹理配合预计算 UV extent、`highp` 采样是否可达到当前像素标准；当前注释说明过去存在精度回归，不能直接删除 padding。
- 若精度仍不满足，考虑按固定 2 的幂 tile 存储/拆分图集，或对特别容易放大的图集采用单独策略。
- 用包含 mask、细线条、透明边缘的 GLES 图像对照作为放行条件。

---

### P2-7：会话加载时完整编译两次几何场景

**结论：代码可证实，是低成本启动/切换优化。**

```cpp
// src/application/session.cpp:160-176
auto geometry = runtime::compile_geometry_scene(model);
...
bool gaze = ...;
for (const auto name : kGazeVariables)
    gaze &= geometry.scene->find_variable(name).valid();
...
geometry = {}; // Binding discovery only; final playback owns its compiled scene.
auto playback = runtime::compile_playback(model, ...);
```

而 `compile_playback()` 内部首先又调用一次 `compile_geometry_scene(asset)`（`src/runtime/playback.cpp:46-54`）。第一次完整结果只用于绑定发现，随后全部销毁。复杂模型切换时，这会重复场景构建、字符串/vector 分配和编译工作。

**建议**：先只调用 `compile_playback()`，再通过 `playback.program->scene()` 做 gaze/lip 绑定发现。失败时沿用现有错误返回。若必须在编译 playback 前发现 source，可把 source 名称检查移到编译后的验证阶段，或让编译器一次性完成所需绑定。

**收益边界**：减少加载/切换 CPU 和瞬时内存，不影响稳态帧成本。

---

### P2-8：Vulkan 资源逐对象分配，纹理上传逐次创建 fence 并等待

**结论：代码可证实，主要影响加载/切换和驱动资源管理，不是稳态每帧问题。**

每个 image 都独立 `vkAllocateMemory`：

```cpp
// src/render_vk/vk_util.hpp:239-247
vkGetImageMemoryRequirements(...);
...
vkAllocateMemory(...);
vkBindImageMemory(...);
```

每张纹理上传又创建 staging、command buffer、fence，提交并立即等待：

```cpp
// src/render_vk/renderer.cpp:424-442
auto staging = create_mapped_buffer(...);
...
const auto upload_commands = begin_one_time_commands(...);
...
end_one_time_commands(...);

// src/render_vk/vk_util.hpp:323-347
vkCreateFence(...);
vkQueueSubmit(...);
vkWaitForFences(...);
vkDestroyFence(...);
```

**建议**

- 一次角色上传使用共享 staging arena 和一批 copy command，最后统一提交/等待一次；
- 复用 command pool/fence，而非逐纹理创建销毁；
- 若纹理数量与 driver allocation 高水位证明有必要，再引入子分配器/VMA；image 子分配需遵守各自的 size/alignment/memoryTypeBits，不能简单把像素拼接后共绑；
- 稳态 mapped VBO/EBO 不需要为此重写。

**验证**：冷/热切换耗时、上传正确性、device local/host visible 内存、多显卡和失败路径。不要把加载阶段 fence 等待误算为稳态帧瓶颈。

---

### P3-9：音频 worker 在空闲和暂停时仍周期唤醒

**结论：代码可证实，优先级低于 PCM 常驻内存。**

```cpp
// src/audio/device.cpp:265
std::this_thread::sleep_for(std::chrono::milliseconds(stream ? 4 : 12));
```

无 stream 时约每 12 ms 唤醒一次；stream 存在时约每 4 ms 一次，暂停只让 `observe()` 提前返回，并没有停止循环。`pa_mainloop_iterate(loop, 0, ...)` 也是非阻塞调用（`233-235`）。

**建议**：在 PulseAudio mainloop 可等待事件的前提下，用阻塞迭代配合 `pa_mainloop_wakeup()` 处理 submit/quit；若 RMS/播放结束确需定时检查，只在 Playing 状态使用短超时，Ready/Paused 使用事件唤醒。提交与退出必须保证唤醒不丢失，避免音频线程无法退出。

---

## 4. 代码质量与可维护性观察

### 4.1 OpenGL 缺少 `view.alpha` 校验，与 Vulkan 契约不一致

Vulkan 明确检查 alpha 有限且在 `[0,1]`：

```cpp
// src/render_vk/renderer.cpp:821-825
!std::isfinite(view.alpha) || view.alpha < 0 || view.alpha > 1
```

OpenGL 对应入口只检查 origin、scale 和 clear（`src/render_gl/renderer.cpp:653-657`），没有 alpha 检查。内部调用目前提供合法值，因此这不是当前稳态热点，但公共 API 对无效输入的行为不一致。建议补上相同校验，并增加负例测试。

### 4.2 两后端重复实现较多，优化应落在共享协议上

GL/Vulkan 的 `prepare()` 都重复容量检查、逐顶点/索引校验、mask bounds、command 保存等逻辑。此次推荐的 revision 若在共享 `RenderFrame`/`PlaybackInstance` 层定义，可以避免两个后端以后出现不同失效规则。后端只保留 API 相关上传，共享层负责内容身份和变化语义。

### 4.3 不建议把 C ABI 通用入口与内部受信热路径混为一谈

`src/abi/abi.cpp:310-351` 和 `355-420` 为通用 C ABI 做逐字段 C++→C、C→C++ 拷贝，随后 `nekomote_renderer_prepare()` 又进入 renderer 校验（`1234-1261`）。这对诊断 ABI 的边界安全是合理的，但内存带宽较高。当前高层 wallpaper 路径直接使用 C++ renderer，不应为了优化诊断 ABI 而破坏其不可信输入校验。若外部宿主成为主要使用方式，再考虑零拷贝只读 span + 明确寿命，而不是简单删除检查。

---

## 5. 不建议优先实施的方向

1. **先做 SIMD、GPU 变形或复杂多线程**：目前没有本次同条件 profile 证明它们是首要瓶颈，而且几何代码对浮点保存顺序有明确兼容性要求，风险高。
2. **为降低显存直接改纹理压缩格式**：BC/ASTC 的能力、质量、导入成本和跨后端一致性都需另行评估，收益不如先去除离屏冗余和 padding 确定。
3. **删除所有帧校验**：对内部受信、revision 未变的帧跳过重复校验更合理；外部 ABI 校验仍有价值。
4. **简单把 FPS 默认降低**：能省资源但牺牲流畅度。先修暂停静止和重复上传，再根据实测提供节能档位。
5. **把绘制命令跨透明层重排合批**：项目明确依赖 painter order，只能合并相邻且状态兼容的命令。

---

## 6. 推荐实施与测量计划

### 阶段 A：低风险、立即可做

1. 实现 GPU frame revision 门控。
2. 三个独立宿主实现暂停静止时 event-driven scheduling。
3. 已知音频设备失败时跳过一次也无用的全量解码。
4. 删除会话加载的第一次冗余 geometry compile。
5. 补齐 GL alpha 校验。

**测量**：固定模型/种子/分辨率，记录暂停 60 秒的线程 CPU 时间、提交帧数、VBO/EBO 上传字节数；恢复后像素一致。

### 阶段 B：常驻内存

1. 语音按需解码 + 有界缓存。
2. 统计真实资产每条 clip 的 PCM 字节数、bank 总量、切换峰值。
3. 评估 S16 或流式解码是否仍有必要。

**测量**：启动、首次播放、连续反应、角色切换、设备失败时的 RSS/PSS；同时记录首播延迟和 underrun，避免只优化内存却破坏音频。

### 阶段 C：GPU/显存

1. direct-over 快路径 + lazy color target。
2. GLES padding 替代方案。
3. mask backing store 按联合包围盒分配。
4. Vulkan 批量上传与资源子分配（有测量后）。

**测量条件必须记录**：提交、Release/LTO、GPU/驱动、桌面与宿主、模型、动作、输出尺寸、FPS、预热、采样区间。分别记录 CPU P50/P95/P99、实际呈现、RSS/PSS、GPU 显存和功耗；不能把截图通过或单个探针数据称为整体性能提升。

---

## 7. 本次审查限制

- 本次为静态代码审查，没有修改项目实现代码。
- 未使用仓库外的真实 NEKOPARA 资产，因此没有给出具体模型的总 PCM、图集或三角形规模。
- 未运行真实 GPU/Plasma/X11/Wayland 性能基准；所有 MiB 数字均由代码中的格式与尺寸公式推导，并明确标为示例。
- 未把仓库既有 `docs/validation` 的历史结果当成本次提交、当前硬件上的新实测结果。
- 报告中的优化项均需按各节列出的正确性条件验证后实施，尤其是 direct rendering、GLES 非 2 的幂纹理和音频懒加载。
