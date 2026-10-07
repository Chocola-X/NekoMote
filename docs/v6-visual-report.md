# V6 视觉证据与源 GPU 条件采样报告

> 报告属性：交接与证据文档  
> 记录日期：2026-10-07  
> 执行者：Antigravity  
> 模型：Gemini 3.8 Flash (High)  
> 基线提交：`6247dbd`（HEAD / G9 检查入口与构建矩阵复核）  
> 任务上下文：`docs/agent-workboard.md` V6  
> 证据目录：`build/evidence/v6/`

---

## 1. 勘误与前置报告修正说明（针对 V3 与 V5 报告）

依据协作清单规范与复核要求，对此前提交中的表述与分析边界作出明确界定和修正：

### 1.1 改动归属与提交边界澄清
- **父提交已有特性**：在父提交 `5a4dc13` 中，UI 在展开状态下拖拽先更新面板坐标的逻辑、`Renderer` 的 `global_alpha` 接口以及 0.28s 展开 / 0.24s 关闭渐变过渡时间均**已经存在**。因此，这些特性属于已有实现，**不属于** `97c9409` 的新修复。
- **`97c9409` 实际提交内容**：实际新增内容为图标视口边界 clamp、失去鼠标按下状态后的拖拽释放、sidebar 最小 44px 高度保护、越界重开自动 reset、纯 CPU 运行渲染守卫（`if (!initialized_) return;`）以及无 GL 状态单元测试（`apps/player/test_ui.cpp`）。

### 1.2 0px 误差结论纠偏
- `test_ui.cpp` 包含 8 组独立状态测试（124 项断言），属于纯逻辑层数据模型测试。
- 标题同步测试仅计算并断言了内存数据结构中的数值偏移（`sidebar.x + 16.0f`, `sidebar.y + 12.0f`），并未测量 OpenGL 渲染管线提交的顶点缓冲区（VBO）或屏幕呈现像素。
- **修正结论**：状态测试通过证明了**数据结构坐标在计算上同步**，不能作为 GPU 实际图元或屏幕呈现“0px 视觉对齐”的证明。动态拖拽与渐变过渡在缺乏应用内逐帧测试入口前，**不可声称动态已验收**。

### 1.3 运行时错误码 6/3 语义澄清
- 在此前的测试记录中，原生播放器遇到未实现的变换抛出退出信息：
  `NekoMote: session failure 6/3`
- **修正说明**：该错误码对应内部 `SessionError::Playback (6)` / `PlaybackError::Geometry (3)`（底层原因为 `UnsupportedTransform`），属于会话与求值模块的内部强类型错误码，**不是公共 C ABI 的错误枚举**（公共 ABI 枚举定义于 `include/nekomote/abi.h` 中的 `NEKOMOTE_STATUS_*`）。两者不可混淆。

### 1.4 V3 源 GPU 观测结论的范围局限
- V3 中所记录的观测为单一 `steps=0` 静态初态样本。虽然在初始帧观测到了 `bm=19` 的 `[774, 771, 0, 1]` 正片叠底与 `attr=2` 的 `[770, 771, 1, 771]` 单遍 Over 绘制，但这只是零步样本，不能直接推论全姿态、全参数或所有有效透明度下的行为。
- 本报告第 2 节补充了 19 组受控条件采样，以确定参数对图元的影响范围与局限性。

---

## 2. 工具与输入冻结账本

所有采样均在无头浏览器环境中，在受控排他锁 `flock -n build/gpu-validation.lock` 保护下执行。相关工具与原始资产的 SHA-256 指纹如下：

| 工具 / 资产 | 文件路径 | SHA-256 哈希值 |
|---|---|---|
| `trace.js` | `tools/reference_capture/trace.js` | `5c7efea770c6499878b2737604f326a27e7ca519a716c52a0fe5c56d0d216524` |
| `capture.py` | `tools/reference_capture/capture.py` | `ee347d4e3cb484439c27ba9e02c5bc56bf6fc4bb5336e890f845d4c65330310e` |
| `capture.cjs` | `tools/reference_capture/capture.cjs` | `b99e909a341aa50f0e0bc45778b0561eefab0ee12f91a5ef8c819ca1e7215906` |
| `reference.html` | `tools/reference_capture/reference.html` | `0f0bf26dff0639d486eecae1ecfa8e2bf57fa3c1a7051df55f0b5d556c5240bc` |
| `sample_conditions.py` | `tools/reference_capture/sample_conditions.py` | `fffeef125ec7a68579d0cbcfb2d4b971a1c97a8d56b0c3d973752e5576a9c141` |
| `compare_ui_static.py` | `tools/reference_capture/compare_ui_static.py` | `41f40aa997f7fa08127bc4f285223ae317929fc12d19b48c66a4f7833077755b` |
| 浏览器二进制 | `/home/chocola/.cache/ms-playwright/chromium-1194/chrome-linux/chrome` | `5b6ca8c422ea77d1cf7a864e26715b9c0375cf3a8d11cbeeb706bf93d8bdf94f` |
| `fraise-maid-a` PSB | `../NekoWebShow/data/fraise-maid-a.pure.psb.zip` | `cea1488619a235d95add88b50ffc8424d4bd1afa7357cd470d69171405d83788` |
| `fraise-france-maid-b` PSB | `../NekoWebShow/data/fraise-france-maid-b.pure.psb.zip` | `0cf635650ba9a1d57c8e1f275c8e1ac54944e3a8bfea69220c548d97940e8fe2` |
| `FreeMoteDriver.js` | `../NekoWebShow/driver/FreeMoteDriver.js` | `0e4d2711e15a9a9addcd0599543f8a5d952abffabfc6c3ba1c94ee51d8557dc7` |
| `emoteplayer.js` | `../NekoWebShow/driver/emoteplayer.js` | `2d6b0b146cdf190fba9cea3c0b759b88c713edf3786a51f6f7b11532ffbb6d4d` |

完整产物账本已归档于 `build/evidence/v6/v6_ledger.json`（共 115 个条目）。

---

## 3. 源 GPU 条件采样（`bm=19` 与 `attr=2`）

通过增强型 `trace.js` 捕获 `colorMask`、`blendEquationSeparate`、`blendFuncSeparate`、FBO 绑定、着色器源码、uniform 矩阵与顶点定位。

### 3.1 案例一：`bm = 19` 条件采样（模型：`fraise-maid-a`）
- **目标图元**：发饰蝴蝶结投射阴影（`icon: "0105"`，图集切片 `left: 1758.0, top: 1395.0, width: 298.0, height: 124.0`，UV 范围 `U ∈ [1758, 2056], V ∈ [529, 653]`）。
- **固定执行尺寸**：800×1080，坐标 (x=0, y=400, scale=1.0)。

#### 10 组采样条件与状态数据对比：

| 采样用例 ID | 条件描述 | Draw ID | 混合方程 (RGB, A) | 混合参数 (sRGB, dRGB, sA, dA) | colorMask | FBO | 屏幕包围盒 [minX, maxX, minY, maxY] | 相对初态位移 (dX, dY) | 索引数 |
|---|---|:---:|---|---|:---:|:---:|---|:---:|:---:|
| `fraise-maid-a_base` | 初态基准（steps=0, opacity=1.0） | #33 | (GL_FUNC_ADD, GL_FUNC_ADD) | (774, 771, 0, 1) | [T,T,T,T] | True | [251.0, 549.0, 270.0, 394.0] | (0.0, 0.0) | 28 |
| `fraise-maid-a_opacity05` | 全局透明度 opacity=0.5 | #33 | (GL_FUNC_ADD, GL_FUNC_ADD) | (774, 771, 0, 1) | [T,T,T,T] | True | [251.0, 549.0, 270.0, 394.0] | (0.0, 0.0) | 28 |
| `fraise-maid-a_head_lr30` | 头部右转（`head_LR: 30`） | #33 | (GL_FUNC_ADD, GL_FUNC_ADD) | (774, 771, 0, 1) | [T,T,T,T] | True | [274.9, 565.1, 269.3, 398.4] | (+23.9, -0.7) | 178 |
| `fraise-maid-a_head_lr_minus30`| 头部左转（`head_LR: -30`） | #33 | (GL_FUNC_ADD, GL_FUNC_ADD) | (774, 771, 0, 1) | [T,T,T,T] | True | [235.3, 524.5, 269.3, 398.6] | (-15.7, -0.7) | 178 |
| `fraise-maid-a_head_ud30` | 头部低头（`head_UD: 30`） | #33 | (GL_FUNC_ADD, GL_FUNC_ADD) | (774, 771, 0, 1) | [T,T,T,T] | True | [251.6, 549.6, 299.1, 437.0] | (+0.6, +29.1) | 178 |
| `fraise-maid-a_head_slant30` | 头部歪头（`head_slant: 30`） | #33 | (GL_FUNC_ADD, GL_FUNC_ADD) | (774, 771, 0, 1) | [T,T,T,T] | True | [285.0, 597.4, 252.8, 417.0] | (+34.0, -17.2) | 28 |
| `fraise-maid-a_body_lr30` | 身体右转（`body_LR: 30`） | #33 | (GL_FUNC_ADD, GL_FUNC_ADD) | (774, 771, 0, 1) | [T,T,T,T] | True | [254.7, 553.5, 270.0, 394.0] | (+3.7, 0.0) | 28 |
| `fraise-maid-a_act_sp30` | 特殊动作（`act_sp: 30`） | #33 | (GL_FUNC_ADD, GL_FUNC_ADD) | (774, 771, 0, 1) | [T,T,T,T] | True | [243.7, 556.6, 278.5, 410.0] | (-7.3, +8.5) | 28 |
| `fraise-maid-a_step30` | 固定步长推进（steps=30） | #33 | (GL_FUNC_ADD, GL_FUNC_ADD) | (774, 771, 0, 1) | [T,T,T,T] | True | [251.0, 549.0, 270.0, 394.0] | (0.0, 0.0) | 28 |
| `fraise-maid-a_face_invariance`| 面部表情变化（眼/口/眉） | **#19** | (GL_FUNC_ADD, GL_FUNC_ADD) | (774, 771, 0, 1) | [T,T,T,T] | True | [251.0, 549.0, 270.0, 394.0] | **(0.0, 0.0)** | 28 |

#### 深度数值与行为分析：
1. **状态机恒定性**：在所有 10 组采样（无论姿态旋转、透视变形、网格细分、步进推进还是透明度变化）中：
   - `blendFuncSeparate` 恒为 `(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE)`（774, 771, 0, 1）；
   - `blendEquationSeparate` 恒为 `(GL_FUNC_ADD, GL_FUNC_ADD)`（32774, 32774）；
   - `colorMask` 恒为全开 `[True, True, True, True]`；
   - 着色器恒为带 Alpha 预乘的片元着色器（`tmp.rgb *= tmp.a; gl_FragColor = tmp;`）；
   - 目标缓冲区恒绑定 FBO 离屏纹理。
2. **有效 Opacity 机制证明**：
   - 在 `fraise-maid-a_opacity05` 中，虽然设置了全局 `--opacity 0.5`，但内部 FBO 绘制流的 Draw #33 中，`a_color` 仍为 `[128, 128, 128, 255]`，着色器输出与混合参数保持不变；
   - 追踪显示，全局透明度仅在最终 FBO 呈现到屏幕的 Blit Pass 中通过 uniform `u_texColor = [1, 1, 1, 0.5]` 统一施加；
   - **结论**：角色内部正片叠底层在离屏 FBO 内以完整局部透明度进行混合，不受外层全局透明度影响。
3. **参数影响边界与局限性验证（Face Invariance）**：
   - 改变眼、口、眉等面部表情参数时，前置面部 Draw Call 数量发生增减，使阴影层 Draw 索引从 #33 移至 #19；
   - 但其网格顶点 `a_pos`、屏幕包围盒 `[251.0..549.0, 270.0..394.0]` 与基准态**完全一致（位移量严格为 0.0px）**；
   - 证明：发饰阴影层处于独立运动学层级，不受面部形变参数干扰。
4. **大角度透视网格细分**：
   - 当 `head_LR` 或 `head_UD` 达到 30 时，图元索引数由 28 提升至 178，网格细分密度显著增加以表现头部曲面透视。

---

### 3.2 案例二：`attr = 2` 条件采样（模型：`fraise-france-maid-b`）
- **目标图元**：胸前围裙装饰点（`icon: "0029"`，图集切片 `left: 4080.0, top: 184.0, width: 11.0, height: 11.0`，UV 范围 `U ∈ [4080, 4091], V ∈ [3901, 3912]`）。
- **固定执行尺寸**：800×1080，坐标 (x=0, y=400, scale=1.0)。

#### 9 组采样条件与状态数据对比：

| 采样用例 ID | 条件描述 | Draw ID | 混合方程 (RGB, A) | 混合参数 (sRGB, dRGB, sA, dA) | colorMask | FBO | 屏幕包围盒 [minX, maxX, minY, maxY] | 相对初态位移 (dX, dY) | 索引数 |
|---|---|:---:|---|---|:---:|:---:|---|:---:|:---:|
| `fraise-france-maid-b_base` | 初态基准（steps=0, opacity=1.0） | #9 | (GL_FUNC_ADD, GL_FUNC_ADD) | (770, 771, 1, 771) | [T,T,T,T] | True | [376.0, 387.0, 938.0, 949.0] | (0.0, 0.0) | 4 |
| `fraise-france-maid-b_opacity05`| 全局透明度 opacity=0.5 | #9 | (GL_FUNC_ADD, GL_FUNC_ADD) | (770, 771, 1, 771) | [T,T,T,T] | True | [376.0, 387.0, 938.0, 949.0] | (0.0, 0.0) | 4 |
| `fraise-france-maid-b_body_lr30`| 身体右转（`body_LR: 30`） | #9 | (GL_FUNC_ADD, GL_FUNC_ADD) | (770, 771, 1, 771) | [T,T,T,T] | True | [391.1, 401.0, 938.1, 949.0] | (+15.1, +0.1) | 196 |
| `fraise-france-maid-b_body_lr_minus30`| 身体左转（`body_LR: -30`） | #9 | (GL_FUNC_ADD, GL_FUNC_ADD) | (770, 771, 1, 771) | [T,T,T,T] | True | [365.9, 377.9, 938.0, 949.8] | (-10.1, 0.0) | 196 |
| `fraise-france-maid-b_body_ud30`| 身体下沉（`body_UD: 30`） | #9 | (GL_FUNC_ADD, GL_FUNC_ADD) | (770, 771, 1, 771) | [T,T,T,T] | True | [383.2, 394.0, 950.0, 960.9] | (+7.2, +12.0) | 196 |
| `fraise-france-maid-b_body_slant30`| 身体倾斜（`body_slant: 30`） | #9 | (GL_FUNC_ADD, GL_FUNC_ADD) | (770, 771, 1, 771) | [T,T,T,T] | True | [402.4, 414.2, 940.6, 952.4] | (+26.4, +2.6) | 196 |
| `fraise-france-maid-b_act_sp30`| 特殊动作（`act_sp: 30`） | #9 | (GL_FUNC_ADD, GL_FUNC_ADD) | (770, 771, 1, 771) | [T,T,T,T] | True | [380.9, 392.8, 947.0, 958.8] | (+4.9, +9.0) | 196 |
| `fraise-france-maid-b_step30` | 固定步长推进（steps=30） | #9 | (GL_FUNC_ADD, GL_FUNC_ADD) | (770, 771, 1, 771) | [T,T,T,T] | True | [375.9, 387.7, 938.0, 949.7] | (-0.1, 0.0) | 196 |
| `fraise-france-maid-b_head_invariance`| 头部旋转耦合测试（`head_LR/UD: 30`）| #9 | (GL_FUNC_ADD, GL_FUNC_ADD) | (770, 771, 1, 771) | [T,T,T,T] | True | [374.2, 385.2, 943.2, 954.2] | (-1.8, +5.2) | 196 |

#### 深度数值与行为分析：
1. **单遍标准 Over 绘制（ColorProtection::None）验证**：
   - 在包含 `attr=1` 的 Sprite 中（例如 Draw #0、#3、#6），WebGL 必定先执行一遍 `blend=(773, 772, 0, 1)`（`GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA`）的颜色保护 Pass；
   - 而所有 9 组条件采样均确认：Draw #9 仅执行单次标准 Over 绘制（`GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA`），片元着色器为直通采样；
   - 证明：**`attr = 2` 的严格语义为单遍普通绘制，不启用双遍透明保护**。
2. **运动学耦合特性**：
   - 躯干旋转与俯仰直接驱动该装饰点（`body_LR=30` 产生 +15.1px 横向位移，`body_UD=30` 产生 +12.0px 纵向位移）；
   - 在 `fraise-france-maid-b_head_invariance` 中，头部偏转带来轻微胸前联动（dX = -1.8px, dY = +5.2px），证明该模型头部骨骼链对胸部预备层有弱运动学继承影响。

---

## 4. 静态 UI 4 态受控核对（WebGL 参照 vs 原生 Player）

使用统一角色 `chocola-date-a`、统一视口 800×1080 对静态 4 态进行比对，生成高倍局部裁切和完整对照图。

### 4.1 4 态对照结果表

| 状态 ID | 状态说明 | 对照图路径 | 结构比对结果 | 视觉风格比对结果 | 结论 |
|---|---|---|:---:|:---:|:---:|
| `closed` | UI 收起态（仅猫爪切换图标） | `build/evidence/v6/ui_comparison/side_by_side_closed.png` | PASS | PASS | **PASS**（尺寸与纹理一致；默认停靠坐标存在 -8px/+8px 微差） |
| `open-character` | 展开态 - 巧克力角色抽屉展开 | `build/evidence/v6/ui_comparison/side_by_side_open-character.png` | PASS | PASS | **PASS**（面板宽度 264px、圆角 18px、标题/选项文字/箭头完全对齐） |
| `open-vanilla` | 展开态 - 香草抽屉展开 | `build/evidence/v6/ui_comparison/side_by_side_open-vanilla.png` | PASS | PASS | **PASS**（手风琴互斥逻辑正确，香草型号列表完全对齐） |
| `open-settings` | 展开态 - 设置抽屉展开 | `build/evidence/v6/ui_comparison/side_by_side_open-settings.png` | PASS | PASS | **PASS**（帧率单选、开关按钮、重设按钮与壁纸单选结构齐备） |

### 4.2 局部裁切与细节度量

1. **猫爪切换按钮（`crop_toggle_icon_compare.png`）**：
   - **尺寸**：Web 与 Native 均为 44×44px 正方形；
   - **纹理**：采用相同的 `neko.png` 图标资产，直通 Alpha 混合；
   - **停靠坐标微差**：Web 在 CSS 中为 `top: 8px; right: 8px;`（对于 800 宽窗口即 `left: 748, top: 8`）；Native 在 `apps/player/main.cpp:750` 中为 `x = fw - 60 (740px), y = 16px`。
   - **判定**：结构与资产一致，初始停靠位置存在 Δx = -8px, Δy = +8px 微量偏移（由 Codex 在布局微调中统一）。
2. **面板尺寸与圆角（`crop_sidebar_open-character_compare.png`）**：
   - 面板宽度恒定为 264px；顶部圆角为 18px；
   - 背景色均为 `rgba(255, 255, 255, 0.78)`；
   - 标题 "猫娘乐园角色E-mote图鉴" 字号、咖啡色 `#6f4e37` 及底部粉色分割线 `#f2a0c0` 表现一致。
3. **字体光栅化微差说明**：
   - Web 浏览器采用系统字体（PingFang SC / Microsoft YaHei）；Native 使用 `stb_truetype` 加载 `Noto Sans CJK SC` 动态图集；
   - 字体边缘平滑度与字偶间距存在像素级正常差异，整体排版与字行高度一致。
4. **边框描边差异**：
   - Web 侧边栏拥有 CSS `border: 2px solid #f2a0c0` 外边框描边；
   - 原生端当前仅渲染圆角背景面板及内部粉色分割线，缺少外层 2px 的描边 Quad（可在后续 UI 装饰 Pass 补全）。

---

## 5. 动态拖拽与渐变过渡接口缺口（面向 Codex C6）

### 5.1 现状与未测项说明
- **未测项**：动态拖拽轨迹、连续手风琴折叠展开过渡动画、自由态淡入淡出（0.28s / 0.24s）的逐帧截图像素核验。
- **原因**：当前原生播放器主要依赖系统 GLFW 事件循环（`glfwPollEvents`），**缺少应用内部确定性、与 OS 隔离的事件回放与固定时间步逐帧截图接口**。
- **规范执行**：根据规范，Antigravity 不向系统注入 OS 键鼠事件，不修改 `main.cpp` 或 `player/ui.cpp` 的事件分发核心，因此**严格将动态视觉标记为未测项（UNTESTED）**，不虚报验收通过。

### 5.2 交付给 Codex 的最小所需回放接口契约建议

建议 Codex 在推进 C6 时，在 `player::ui::Context` 与 `apps/player/surface.hpp` 提供以下确定性回放诊断入口：

```cpp
// 建议在 apps/player/ui.hpp 或测试 harness 中暴露：
namespace nekomote::player::ui {

struct SimulatedMouseEvent {
    enum class Type { Move, Down, Up, Scroll };
    Type type;
    double x;
    double y;
    int button{0};
    double scroll{0.0};
    float timestamp{0.0f}; // 确定性步进时间
};

// 允许在 headless / test_ui 中无窗口、无系统事件地回放序列
void replay_mouse_events(Context& ctx, const std::vector<SimulatedMouseEvent>& events, float delta_time);

}
```

待 Codex 闭合回放接口后，即可针对动态拖拽和逐帧渐变开展自动化无闪烁逐帧像素比对。

---

## 6. 证据产物汇总与交接清单

| 产物文件路径 | 类型 | 校验信息 / 关键数据 |
|---|---|---|
| `build/evidence/v6/bm19_condition_sampling.json` | JSON | 10 组 `bm=19` 完整 GPU 状态、MVP 矩阵与定位数据 |
| `build/evidence/v6/attr2_condition_sampling.json` | JSON | 9 组 `attr=2` 完整 GPU 状态与单遍 Over 绘制证明 |
| `build/evidence/v6/tool_hashes.json` | JSON | 所有测试脚本与输入资产 SHA-256 账本 |
| `build/evidence/v6/ui_static_comparison.json` | JSON | 静态 4 态几何度量、颜色及差异审计数据 |
| `build/evidence/v6/v6_ledger.json` | JSON | v6 目录下全部 115 个文件完整哈希账本 |
| `build/evidence/v6/crop_fraise-maid-a_*.png` | PNG | 10 组 `bm=19` 投射阴影高倍局部裁切 |
| `build/evidence/v6/crop_fraise-france-maid-b_*.png` | PNG | 9 组 `attr=2` 装饰点高倍局部裁切 |
| `build/evidence/v6/ui_comparison/*.png` | PNG | 4 态 UI 完整 Side-by-Side 对照图与局部关键组件裁切 |
| `build/evidence/v6/web_ui/*.png` | PNG | 4 态 WebGL 参考播放器截图（`chocola-date-a`） |
| `build/evidence/v6/native_ui/*.png` | PNG | 4 态原生播放器实机截图（`chocola-date-a`） |

---

## 7. 验收与下一步

- **V6 任务目标全部闭合**：
  1. 完成 V3 / V5 报告勘误与归属界定；
  2. 完成 `bm=19` 与 `attr=2` 的 19 组受控条件采样，证明混合方程不变性与参数边界局限；
  3. 完成静态 UI 4 态与 WebGL 参考的受控对照与局部裁切；
  4. 明确动态拖拽为未测项，输出所需最小回放接口契约。
- **后续接力**：交由 **Codex** 优先在 C5 中闭合 `bm19` / `attr2` 的原生求值与渲染映射，并在 C6 中补齐应用回放入口。
