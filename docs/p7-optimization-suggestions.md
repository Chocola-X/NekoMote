# P7 渲染核心代码优化建议

> 2026-10-07审查：本文件是静态代码候选清单，**没有profile证据证明13项都是实际瓶颈或高收益**，不授权按第一批整体实施。关联性能报告的方法未验收，原生优势倍数撤回认证。先解决R1–R6及G5，再对实际瓶颈逐项比较，详见 [审查](review-2026-10-07.md)。


**日期**：2026-10-07
**提交**：869a960
**基于**：[性能对比报告](p7-webgl-vs-native-performance.md) + 渲染核心代码逐行通读
**方法**：通读 `src/runtime/`、`src/render_gl/`、`src/application/` 共 4851 行代码，列出可定位的候选操作；静态存在调用不证明其开销占比、收益或语义可安全改变。

## 性能对比结果摘要

| 指标 | 原生 (player) | WebGL (chromium) | 历史比值（不认证收益） |
|---|---|---|---|
| CPU 占用率 | 7.46% | 41.13% | 5.5x |
| 内存 RSS | 149.73 MiB | 671.27 MiB | 4.5x |
| GPU 利用率 | 23.74% | 27.62% | 1.2x |
| 显存占用 | 993 MiB | 1182 MiB | 1.2x |
| render+swap P50 | 0.25 ms (vsync=0) | — | — |
| rAF 间隔 P50 | — | 16.7 ms (60 FPS) | — |

上表保留原测量的算术比值；条件未对齐，不能归纳为性能优势。以下候选需要各自profile和正确性证据。

---

## 一、局部候选（收益与风险待验证）

### 1. `texture()` 线性查找改为直接索引

- **位置**：`src/render_gl/renderer.cpp:267-272`（定义），`:569`/`:589`（调用）
- **当前**：`std::find_if` 线性查找 TextureId，每个 draw command + mask command 调用一次
- **问题**：O(textures.size()) 查找。TextureId 在 `geometry_scene.cpp:457` 编译时连续递增分配，可直接索引
- **建议**：增加 `std::vector<GLuint> texture_handles;`（按 `TextureId.value` 索引），`texture()` 改为 `return id.value < texture_handles.size() ? texture_handles[id.value] : 0;`
- **收益**：每帧消除 `(commands + mask_commands) * textures / 2` 次比较。50 纹理、100 command 场景省约 2500 次比较 + lambda 调用
- **风险**：低。需确认 TextureId 连续性（已确认编译时递增）

### 2. `build_frame` 中无条件调用 `deform`

- **位置**：`src/runtime/geometry_scene.cpp:1105`
- **当前**：每个网格顶点都调用 `instance.deform(p.node, rigid_point)`，`deform`（`:674-684`）沿 `curve_parent` 链遍历
- **问题**：`mesh_path` 已在 `evaluate()` `:813` 计算。`mesh_path == false` 时 `deform` 的 while 循环中 `s.transform.mesh` 恒 false，是空操作。一个 division=10 的网格精灵有 121 顶点，几十个精灵 = 数千次空调用
- **建议**：`const auto& state = instance.states[p.node.value]; const auto point = state.mesh_path ? instance.deform(p.node, rigid_point) : rigid_point;`
- **收益**：每个无网格变形的精灵省 `(nx+1)*(ny+1)` 次 `deform` 调用（函数调用 + while 迭代 + curve_parent 加载）
- **风险**：极低。`mesh_path` 语义已验证，false 时 deform 返回原值

### 3. 零角度节点未跳过 `cos`/`sin`

- **位置**：`src/runtime/geometry_scene.cpp:849-855`
- **当前**：每个活跃节点每帧执行 `fmod` + 2 次 `std::cos`/`std::sin`，即使 `angle_degrees == 0`
- **问题**：`std::cos`/`std::sin` 是 libm 调用（每次约 20-40 周期）。许多节点是纯平移节点（angle=0），cos=1、sin=0 是平凡的
- **建议**：`if (state.transform.angle_degrees != 0.0f) { ... } else { cosine = 1.0f; sine = 0.0f; }`
- **收益**：每个零角度节点省 1 次 `fmod` + 2 次 libm 调用。数百节点中若大部分无旋转，每帧省数百次 libm 调用
- **风险**：低。`angle == 0` 时 cos/sin 精确为 1/0

### 4. `node_commands` 每帧全量 `std::fill`

- **位置**：`src/runtime/geometry_scene.cpp:1046`
- **当前**：`std::fill(instance.node_commands.begin(), instance.node_commands.end(), DrawCommand{});`，大小 = `nodes.size()`
- **问题**：O(nodes.size()) 清零，但只有 `primitives.size()` 个条目被写入（`:1132`）。nodes 可达数千，primitives 通常几十到几百
- **建议**：记录上一帧写入的 node 索引，下一帧只清除那些。或用 `std::vector<NodeId> used_node_commands;` 跟踪
- **收益**：每帧 O(nodes) → O(primitives)。nodes=2000、primitives=100 的模型省 1900 次 DrawCommand 清零
- **风险**：低。只需正确跟踪上一帧写入索引

### 5. 顶点循环重复除法可预计算倒数

- **位置**：`src/runtime/geometry_scene.cpp:1080,1090-1091,1101,1108-1109`
- **当前**：每个顶点做 `y / ny`、`x / nx`、`u / texture.width`、`v / texture.height` 等除法
- **问题**：`:1091` 的 `y/ny` 与 `:1080` 的 `row` 重复；`:1090` 和 `:1101` 都做 `x/nx`；除法比乘法慢 3-5 倍
- **建议**：primitive 循环开头预计算 `inv_nx = 1.0/nx`、`inv_ny = 1.0/ny`、`inv_tw = 1.0f/texture.width`、`inv_th = 1.0f/texture.height`，顶点循环用乘法
- **收益**：每个顶点省 3-4 次浮点除法。网格精灵顶点数可达数百/数千
- **风险**：低。double 精度下 1/nx 精确计算后乘，差异可忽略。需确认 UV 的 Float32 保存边界不变

### 6. `use_mask` uniform 重复设置

- **位置**：`src/render_gl/renderer.cpp:586-588`
- **当前**：无 mask 的 command 每次都 `glUniform1i(p.use_mask, 0)`
- **问题**：如果前一个 command 也没有 mask，此设置冗余
- **建议**：缓存当前 `use_mask` 状态，只在变化时调用 `glUniform1i`
- **收益**：每帧消除大部分 command 的 `glUniform1i` 调用
- **风险**：低。状态缓存是常见 GL 优化模式

### 7. `identity_patch()` 重复计算

- **位置**：`src/runtime/deformation.cpp:43-48`
- **当前**：`add_patch_displacement` 每次调用 `identity_patch()`，后者循环 16 次填充常量数组
- **问题**：`identity_patch()` 无参数返回常量，每次重新构造
- **建议**：`static constexpr Patch kIdentity = identity_patch();` 或 `static const Patch neutral = identity_patch();`
- **收益**：每个 combine 节点省 16 次 float 构造。combine 节点通常不多，收益较小
- **风险**：极低

---

## 二、中收益中风险（建议评估后实施）

### 8. `glBlendFuncSeparate` 每 command 重复设置

- **位置**：`src/render_gl/renderer.cpp:600-601`
- **当前**：主循环每个 command 末尾无条件设置 blend，但 `straight` 是 render 调用常量
- **问题**：无 protection pass 的 command（大多数）blend 设置与上一帧相同，N 个 command = N 次冗余 GL 调用
- **建议**：缓存当前 blend 状态，只在变化时调用。或只在 protection pass 结束后恢复一次
- **收益**：每帧消除 N-1 次 `glBlendFuncSeparate` 调用
- **风险**：中。需确保 mask write 路径（`:564`）后正确恢复

### 9. `command_bounds` 对所有 command 计算但仅 mask command 使用

- **位置**：`src/render_gl/renderer.cpp:432-434`（计算），`:541`（使用，有 `command.mask.valid()` 守卫）
- **当前**：prepare 中对所有 command 遍历索引计算 bounds，但 render 中只有 mask command 使用
- **问题**：O(total indices) 每帧，大部分 command 无 mask
- **建议**：延迟计算：render 中遇到 `command.mask.valid()` 时才算该 command 的 bounds；或 prepare 中只对 mask command 计算
- **收益**：每帧消除 `(commands - masked_commands)` 次 bounds 计算。无 mask 场景完全省去
- **风险**：中。需重构 command_bounds 存储和索引

### 10. `commands`/`mask_commands`/`masks` 每帧 `assign` 拷贝

- **位置**：`src/render_gl/renderer.cpp:457-459`
- **当前**：prepare 把 frame 的 commands/mask_commands/masks 拷贝到 renderer 内部 vector
- **问题**：`assign` 做元素拷贝（DrawCommand 24 字节，100 command = 2400 字节）。frame 的 span 在下次 build_frame 前有效
- **建议**：在 `Impl` 中保存 `RenderFrame`（含 span）而非拷贝，`prepare` 存 `p.frame = frame;`，`render` 直接用 `p.frame.commands`
- **收益**：每帧消除 3 次 memcpy
- **风险**：中。需验证 prepare 和 render 之间 frame 数据生命周期。若调用方在 prepare 后修改 geometry，span 悬空。需在文档约束

### 11. release 构建中顶点 `isfinite` 验证

- **位置**：`src/render_gl/renderer.cpp:383-393`
- **当前**：prepare 对每个顶点做 6 次 `std::isfinite` + 每个索引做范围检查
- **问题**：`geometry_scene.cpp` 的 `build_frame` 已通过 `evaluate` 中的 `require(std::isfinite(...))` 保证顶点有限性。稳态下此验证是冗余防御性检查。O(vertices * 6 + indices) 每帧
- **建议**：改为 `#ifndef NDEBUG` 守卫，release 跳过。或用 `assert`
- **收益**：每帧省 O(vertices * 6 + indices) 次 `isfinite` 调用。数千顶点 + 数万索引 = 数万次操作
- **风险**：中。移除后若 geometry 产生非有限值会传入 GL。需确认 geometry 不变量在 release 下成立。AGENTS.md §8："断言用于开发不变量，不承担正式输入错误处理"——但此验证当前是正式错误处理，需确认是否可降级为断言

---

## 三、高收益高风险（需 API 变更或精度验证）

### 12. `gaze` 每帧最多 8 次 `transition_difference` 各触发全量 `compose`

- **位置**：`src/application/session.cpp:358-379`（gaze），`src/runtime/animation.cpp:965`（compose），`src/runtime/playback.cpp:123`（compose）
- **当前**：`gaze()` 最多 8 次 `transition_difference()`，每次末尾调用 `data.compose()`，compose 遍历整个 output（数百参数）3 轮
- **问题**：鼠标在窗口内时每帧 8 次 animation compose + 8 次 playback compose = 16 次 O(output) 遍历
- **建议**：批量设置 gaze 参数后只 compose 一次。需 animation/playback 暴露不 compose 的内部设置接口，或增加批量 API
- **收益**：鼠标在窗口时每帧省 7 次 animation compose + 7 次 playback compose
- **风险**：高。需修改 animation/playback API。AGENTS.md §4 要求显式接口，不能加无语义 bool 参数

### 13. `animation::compose` 三次遍历 output 可合并为两次

- **位置**：`src/runtime/animation.cpp:851-900`
- **当前**：第 1 次清零 → 第 2 次叠加 differences → 第 3 次设置 base + blink → 第 4 次叠加 clip
- **问题**：第 1 次清零和第 3 次设置 base 可合并：先设 `value = primary`，再 `value += differences`，不需要清零
- **建议**：交换第 2、3 次循环顺序，移除清零循环
- **收益**：每次 compose 省 1 次 O(output) 遍历。compose 每帧调用多次
- **风险**：中（精度）。当前 `0 + differences + base`，改为 `base + differences`。浮点加法顺序改变可能产生最后一位 ULP 差异。AGENTS.md §6 要求精度证据，需用参考验证（阈值 1e-5）

---

## 实施建议

### 第一批（仅筛选候选，暂不批量实施）

优化点1–7先profile，每次只实施已测瓶颈并单独保留数值/几何/图像回归。不能事先宣称无精度变化，CPU单测/runtime_audit不替代GPU图像验收。

### 第二批（需小幅重构）

优化点 8-11。涉及 renderer 内部状态管理或验证策略调整。建议逐项实施，每项单独验证。

### 第三批（需 API 变更或精度验证）

优化点 12-13。优化点 12 需修改 animation/playback API，建议在 P7 打包阶段统一考虑。优化点 13 需用 `docs/validation/` 下的参考数据验证精度不变。

### 不建议的优化

- **SIMD 向量化**：当前无 profile 证据表明 SIMD 是瓶颈，且 AGENTS.md §6.3 要求"SIMD 必须附性能证据"
- **多线程并行**：AGENTS.md §6.1 明确"初始单线程，有 profile 证据后才引入任务系统"
- **自定义分配器**：零分配已验证，无证据表明分配器是瓶颈
- **GPU 变形**：当前整卡GPU采样不能证明帧瓶颈归属；没有profile前不引入。

---

## 验证方法

每项优化实施后：
1. `ctest --preset release` 确认 128/128 测试通过
2. `nekomote_preview --asset assets/characters/chocola-date-a/model.nkm.json --playback --width 1920 --height 1080 --benchmark 300 --hidden` 确认 render+swap P50 不劣化
3. `python3 tools/perf_compare/perf_compare.py` 确认资源占用不劣化
4. 对优化点 13（精度相关）：用 `tools/reference_capture/validate_animation.py` 确认画面/几何对照不超阈值

## 审查补充：不可直接采用的建议

- 纹理ID的连续性仅在编译模型内成立，宿主背景可使用额外ID。直接按ID扩容必须验证稀疏ID和资源寿命，不能假定所有上传都来自geometry编译器。
- mesh_path=false时细分本来只有4点，不能用121点估计该路径收益；当前局部继承还未闭合，不能把相关标志当作已证明不变量。
- 第10项借用span改变既有“存储活到prepare返回”的契约，涉及旧/新角色同时绘制和ABI；收益需要测量，不为少量复制引入隐含跨调用寿命。
- 第11项Renderer接收宿主公开绘制协议；Release必须验证不可信输入。不能以内部runtime某个检查存在而删除公共错误处理。若分离可信缓存与边界验证，先明确谁验证、缓存失效和失败输出。
- 第13项更改浮点累加顺序，项目已经有ULP影响物理和边缘像素的实证。仅标量1e−5通过不够，仍需冻结几何.001px和图像2/255通过。
