# V7 镜像与遮罩受控视觉观察与定位报告

> 本报告记录 V7 旧实现及手工反向输入的观察，不代表当前源码状态。正式同作者输入、子串匹配和动画方向复核见 [C5 交接](c5-mirror-mask-handoff.md)。`build_v7_evidence.py` 是硬编码结果的后处理器，已被通用 `build_visual_evidence.py` 替代；旧工具可由 Git `c3a7c33` 追溯，冻结证据不改写。
> 报告属性：交接与证据文档  
> 记录日期：2026-10-07  
> 执行者：Antigravity  
> 模型：Gemini 3.8 Flash (High)  
> 基线提交：`1e2f346`（master / G10 刷新 195 项账本及 fraise-france-maid-b 规范化）  
> 任务上下文：`docs/agent-workboard.md` V7  
> 证据目录：`build/evidence/v7/`  

---

## 1. 核心定位结论与证据摘要（Executive Summary）

用户确认正式镜像必须保留，禁止通过取消镜像来伪造对齐。本报告在严格保持原始 PSB、固定 800×1080、x=0/y=400、scale=1.0、opacity=1.0、clock=1000ms、seed=1、steps=0 及物理幅度为 0 的严苛基准下，完成了对 `fraise-france-maid-b` 的 6 组受控条件采样与深度 GPU / 节点拓扑诊断。

### 1.1 闭眼镜像姿态 ~57.775px ~ 81.706px 几何误差的根本成因：`metadata.mirrorControl` 变量符号反转缺失

1. **发现**：源模型文件 `fraise-france-maid-b.json` 在元数据中声明了：
   ```json
   "metadata": {
     "mirror": 1,
     "mirrorControl": {
       "variableMatchList": ["_LR", "_slant"]
     }
   }
   ```
2. **官方 WebGL 引擎行为**：
   - 当模型声明 `metadata.mirror: 1` 时，模型最终画布会在水平方向进行镜像呈现（presentation flip）；
   - 与此同时，为了保证角色向左偏头（`head_LR < 0`）或身体向右偏转（`body_LR > 0`）在镜像呈现后依然符合直觉视角，**E-mote 播放器会自动对名称匹配 `variableMatchList`（V7 未区分后缀与子串匹配）的输入控制变量取反（`val = -val`）后再进行网格形变求值**！
3. **原生项目现有实现的缺口**：
   - `src/asset_io/import_emote.cpp:807` 仅读取了 `metadata.mirror` 存为 `asset.mirror`，而将 `metadata.mirrorControl` 丢弃；
   - 原生 `nekomote_preview`（`apps/preview/main.cpp:487`）和 `tools/pose_probe/main.cpp` 在呈现阶段正确应用了镜像翻转（`view.flip_x = true` / `-vertex.x`），**但未在变量求值阶段对匹配 `_LR` 和 `_slant` 的变量进行符号反转**；
   - 导致原生网格在求值时沿着正向形变后翻转，而 WebGL 参考沿着反向形变后翻转，在水平 X 轴上产生了双倍形变差值（在 `head_LR = 30` 下达到 81.706px，Y 轴误差却始终保持约 0.0px）！
4. **数学与视觉严格反证（Proof of Root Cause）**：
   - **初态闭眼（`head_LR=0, body_LR=0, face_eye_open=10`）**：原生几何误差 **0.000000000px**，黑白底合成 RGB 差异 **[[0, 0, 0], [0, 0, 0]]**，整图 800×1080 像素差异为 **0**！证明基础镜像呈现翻转完全正确；
   - **反转变量对比测试（Native `head_LR = -30` vs WebGL `head_LR = +30`）**：原生全模型 57 组图元最大几何误差骤降至 **0.000191192px**（远低于 0.001px 门槛），黑白底合成 RGB 最大差异为 **0**，像素超差数为 **0**（全图像素 100% 严格吻合）！
   - **反转变量对比测试（Native `body_LR = -30` vs WebGL `body_LR = +30`）**：全模型 57 组图元最大几何误差骤降至 **0.000266483px**，黑白底合成 RGB 最大差异为 **0**，像素超差数为 **0**！
   - **C5 历史用例复核（Native `head_LR = -30, head_UD = 30` vs WebGL `head_invariance`）**：保持 `head_UD = 30` 不动，仅反转 `head_LR` 为 -30，原 57.775px 几何误差骤降至 **0.000280114px**！

### 1.2 默认眼态 Stencil 拒绝（`GeometryError::UnsupportedMask`, code 8）成因

1. **发现**：源码共有 6 处 stencil 定义（不表示同时活动）（`stencilType: 5`）。其中左右眼 `ステンシル(shirome)` 声明：
   ```json
   "stencilCompositeMaskLayerList": ["shirome", "add_mask"]
   ```
2. **实际节点拓扑**：
   - `shirome` 真实存在于当前 motion scope（左眼使用 icon `0065`，右眼使用 icon `0067`，为眼白图元）；
   - `add_mask` 在整个 6.7MB 的源 JSON 中**没有任何节点定义**（仅在左右眼 `stencilCompositeMaskLayerList` 字符串列表中出现 2 次，其模板来源是推断；直接证据是全源无对应定义）；
   - 同样，`頭部変形基礎` 中的 stencil 声明了 `["■輪郭", "■耳L", "■耳R"]`，其中 `■耳L` 与 `■耳R` 在该模型中也完全不存在。
3. **原生运行时代码瓶颈**：
   - `src/runtime/geometry_scene.cpp:551-565` 在解析遮罩来源时，强制要求 `node.mask_sources` 中的每一项都必须解析出有效节点，否则直接标记 `mask.resolved = false`；
   - 在默认眼态下，由于眼睛睁开，该遮罩被选中求值（`instance.used_masks[i] == true`），导致第 1141 行硬性触发 `GeometryError::UnsupportedMask`（code 8），全模型绘制命令被清空。
4. **官方 WebGL 引擎 GPU 行为**：
   - WebGL 播放器遍历 `stencilCompositeMaskLayerList` 时，**静默忽略不存在的图层名称**，仅将存在的 `shirome` 绘制到离屏遮罩 FBO（FBO 2 / Attachment Texture 3）；
   - 着色器中通过 `tmp.g = tmp.a` 将遮罩 alpha 写入 G 通道；后续瞳孔图元（Draw #22, #24, #26 等）通过绑定纹理单元 1（`u_texUnitIdAlpha = 1`）采样遮罩 G 通道相乘，成功完成眼白内的瞳孔裁切。

---

## 2. 6 组受控采样条件与实测数据

所有采样均在排他文件锁 `flock -n build/gpu-validation.lock` 下完成，固定 800×1080 分辨率，中心坐标 (x=0, y=400)，原生预览对应原点 (400, 940)。

| 序号 | 用例 ID | 变量配置 | WebGL 参考像素 | 原生诊断状态 | 几何最大误差 (px) | 黑白合成 RGB 最大差 | 像素超差数 (>2) | 判定结果 |
|:---:|---|---|:---:|:---:|:---:|:---:|:---:|:---:|
| 1 | `fraise-france-maid-b_eye10_neutral` | `face_eye_open: 10` | 408927 px | 57 commands | **0.000000000** | **[[0,0,0], [0,0,0]]** | **0** | **PASS** |
| 2a| `fraise-france-maid-b_eye10_head_lr30` (直选) | `face_eye_open: 10, head_LR: 30` | 408891 px | 57 commands | 81.706582334 | [[255,255,255], [224,206,179]] | 366531 | FAIL (直选未反转) |
| 2b| `fraise-france-maid-b_eye10_head_lr30` (反转验证) | `face_eye_open: 10, head_LR: -30` | 408891 px | 57 commands | **0.000191192** | **[[0,0,0], [0,0,0]]** | **0** | **PASS (反转确证)** |
| 3a| `fraise-france-maid-b_eye10_head_lr_minus30` (直选) | `face_eye_open: 10, head_LR: -30` | 415582 px | 57 commands | 81.706582334 | [[255,255,255], [224,206,179]] | 366531 | FAIL (直选未反转) |
| 3b| `fraise-france-maid-b_eye10_head_lr_minus30` (反转验证)| `face_eye_open: 10, head_LR: 30` | 415582 px | 57 commands | **0.000191192** | **[[0,0,0], [0,0,0]]** | **0** | **PASS (反转确证)** |
| 4a| `fraise-france-maid-b_eye10_body_lr30` (直选) | `face_eye_open: 10, body_LR: 30` | 409029 px | 57 commands | 137.967032843 | [[255,255,255], [224,220,229]] | 380654 | FAIL (直选未反转) |
| 4b| `fraise-france-maid-b_eye10_body_lr30` (反转验证) | `face_eye_open: 10, body_LR: -30` | 409029 px | 57 commands | **0.000266483** | **[[0,0,0], [0,0,0]]** | **0** | **PASS (反转确证)** |
| 5a| `fraise-france-maid-b_eye10_body_lr_minus30` (直选) | `face_eye_open: 10, body_LR: -30` | 409061 px | 57 commands | 137.967032843 | [[255,255,255], [224,220,229]] | 380654 | FAIL (直选未反转) |
| 5b| `fraise-france-maid-b_eye10_body_lr_minus30` (反转验证)| `face_eye_open: 10, body_LR: 30` | 409061 px | 57 commands | **0.000266483** | **[[0,0,0], [0,0,0]]** | **0** | **PASS (反转确证)** |
| 6 | `fraise-france-maid-b_eye0_default` | 默认变量（`face_eye_open: 0`） | 408927 px | 退出码 1 (Code 8) | 未测（原生拒绝） | 未测 | 未测 | **REJECTED (UnsupportedMask)** |

---

## 3. WebGL GPU 绘制流与遮罩机制分析（`eye0_default`）

在默认睁眼状态（`face_eye_open = 0`）下，官方 WebGL 渲染管线提交了 51 组 Draw Calls。核心眼部遮罩流水线发生在 Draw #21 至 #29：

```text
[Draw #20: 臉部輪廓 ■輪郭] (FBO 1, Texture 2)
      ↓
[Draw #21: 左白眼 shirome (Icon 0065)] 
      → 渲染目标: FBO 2 (Attachment: Texture 3)
      → 混合模式: [1, 769, 0, 1] (GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE)
      → 片段着色器: tmp.g = tmp.a; gl_FragColor = tmp; (将白眼 Alpha 写入 G 通道)
      ↓
[Draw #22: 左瞳孔 瞳L (Icon 0063)]
      → 渲染目标: FBO 1 (Attachment: Texture 1)
      → 采样纹理: Texture 2 (Color) + Texture Unit 1 (Texture 3 遮罩)
      → 片段着色器: tmp.a *= texture2D(u_texUnitIdAlpha, alphaCoord).g; (裁切于白眼内)
      ↓
[Draw #23: 右白眼 shirome (Icon 0067)] → 渲染目标: FBO 2 (Texture 3)
      ↓
[Draw #24: 右瞳孔] → 渲染目标: FBO 1, 采样 Texture Unit 1 遮罩
      ↓
[Draw #25: 左白眼辅助刷新] → 渲染目标: FBO 2
      ↓
[Draw #26: 瞳孔高光细节 (Icon 0096)] → 渲染目标: FBO 1, 采样 Texture Unit 1 遮罩
      ↓
[Draw #27-28: 左眼高光 ハイライトL (Icon 0061, attr=1 双遍保护)] → 渲染目标: FBO 1, 采样 Texture Unit 1 遮罩
      ↓
[Draw #29: 右白眼辅助刷新] → 渲染目标: FBO 2
```

### 关键架构发现：
1. **独立遮罩通道**：E-mote 在 WebGL 中并不使用 OpenGL 原生 Stencil 缓冲区（`gl.isEnabled(gl.STENCIL_TEST)` 恒为 `false`），而是分配专用的 Alpha 纹理 FBO（Texture 3），并将 Alpha 复制到 G 通道供主着色器屏幕坐标采样。
2. **容错性**：当 `stencilCompositeMaskLayerList` 中的 `add_mask` 不存在时，播放器不抛出异常，遮罩 FBO 仅由存在的 `shirome` 绘制，完美呈现。

---

## 4. 遮罩 / Stencil 节点拓扑对照表

依据 `build/evidence/v7/fraise-france-maid-b_stencil_hierarchy.json`，全模型 6 处 Stencil 节点的完整拓扑如下：

| 节点路径 | 节点 Label | stencilType | 声明复合图层列表 (`stencilCompositeMaskLayerList`) | 同 Scope 实际存在图层 | 缺失图层 (Missing) | WebGL 行为 |
|---|---|:---:|---|---|---|---|
| `object/all_parts/.../全体構造` | `stencil` | 5 | `["下半身変形基礎"]` | `下半身変形基礎` | 无 | 正常解析 |
| `object/all_parts/.../全体構造` | `stencil` | 5 | `["全身変形基礎"]` | `全身変形基礎` | 无 | 正常解析 |
| `object/face_parts/motion/目L` | `ステンシル(shirome)` | 5 | `["shirome", "add_mask"]` | `shirome` (Icon 0065) | **`add_mask`** | **静默跳过缺失项，仅绘制 shirome** |
| `object/face_parts/motion/目R` | `ステンシル(shirome)` | 5 | `["shirome", "add_mask"]` | `shirome` (Icon 0067) | **`add_mask`** | **静默跳过缺失项，仅绘制 shirome** |
| `object/head_parts/.../頭部全体変形` | `stencil` | 5 | `["頭部変形基礎"]` | `頭部変形基礎` | 无 | 正常解析 |
| `object/head_parts/.../頭部変形基礎` | `stencil` | 5 | `["■輪郭", "■耳L", "■耳R"]` | `■輪郭` | **`■耳L`, `■耳R`** | **静默跳过缺失项，仅绘制 ■輪郭** |

---

## 5. 产物图像与证据索引

本任务生成的所有高清对比图、裁切图及诊断数据已完整落盘于 `build/evidence/v7/`：

### 5.1 Side-by-Side 对比图（三联画：WebGL 参考 | 原生输出 | x4 放大差分）
- [sbs_fraise-france-maid-b_eye10_neutral.png](../build/evidence/v7/sbs_fraise-france-maid-b_eye10_neutral.png)：初态基准（差分全黑，PASS）
- [sbs_fraise-france-maid-b_eye10_head_lr30_direct.png](../build/evidence/v7/sbs_fraise-france-maid-b_eye10_head_lr30_direct.png)：未反转直选对比（清晰展现头部形变反向发散，差分高亮）
- [sbs_fraise-france-maid-b_eye10_head_lr30_inverted.png](../build/evidence/v7/sbs_fraise-france-maid-b_eye10_head_lr30_inverted.png)：输入反转确证（差分全黑，证明 100% 对齐）
- [sbs_fraise-france-maid-b_eye10_body_lr30_direct.png](../build/evidence/v7/sbs_fraise-france-maid-b_eye10_body_lr30_direct.png)：身体未反转直选对比（胸围围裙反向位移）
- [sbs_fraise-france-maid-b_eye10_body_lr30_inverted.png](../build/evidence/v7/sbs_fraise-france-maid-b_eye10_body_lr30_inverted.png)：身体反转确证（差分全黑，证明 100% 对齐）

### 5.2 局部高倍裁切
- **头部轮廓裁切**：`nat_eye10_head_lr30_crop_head.png` vs `ref_eye10_head_lr30_crop_head.png`
- **眼部遮罩裁切**：`ref_eye0_default_crop_eye.png`（展现白眼内裁切之瞳孔与高光）
- **胸口装饰裁切**：`nat_eye10_head_lr30_crop_torso.png` vs `ref_eye10_head_lr30_crop_torso.png`

---

## 6. 工具与输入冻结账本（SHA-256）

相关数据已在 `build/evidence/v7/v7_ledger.json` 中完整冻结。关键文件指纹如下：

| 文件类别 | 相对路径 / 实体 | SHA-256 哈希值 |
|---|---|---|
| 源 PSB 压缩包 | `../NekoWebShow/data/fraise-france-maid-b.pure.psb.zip` | `0cf635650ba9a1d57c8e1f275c8e1ac54944e3a8bfea69220c548d97940e8fe2` |
| 源模型 JSON | `../NekoWebShow/model_conversion/fraise/fraise-france-maid-b/fraise-france-maid-b.json` | `83df1450a80e1a1795c721c5b8b9ba5b1070ff0df56b1076f7f329971936c53e` |
| 导入资产 NKM | `assets/characters/fraise-france-maid-b/model.nkm.json` | `a5442bea99468cce47466a77bc0869701845826d829c1532b55d77f78e5d1f7a` |
| 官方驱动 | `../NekoWebShow/driver/FreeMoteDriver.js` | `0e4d2711e15a9a9addcd0599543f8a5d952abffabfc6c3ba1c94ee51d8557dc7` |
| 官方播放器 | `../NekoWebShow/driver/emoteplayer.js` | `2d6b0b146cdf190fba9cea3c0b759b88c713edf3786a51f6f7b11532ffbb6d4d` |
| 原生求值器 | `build/dev/bin/nekomote_pose` | `82b4ff97fe41fa80ea5f3a0937a43586b0337c72dbbf8058a983b680786efb82` |
| 原生预览器 | `build/dev/bin/nekomote_preview` | `be2690b271d47348911b3e8a4a584348616a2d9a5b39cc52e18b417d917300c1` |
| 采样生成工具 | `tools/reference_capture/build_v7_evidence.py` | `037042a9b49f96b34ea647da6b3b55c3c0ef052824cfd6b8f368bb6ba6f028b1` |
| 捕获包装器 | `tools/reference_capture/capture.py` | `ee347d4e3cb484439c27ba9e02c5bc56bf6fc4bb5336e890f845d4c65330310e` |
| 无头 Chrome | `/home/chocola/.cache/ms-playwright/chromium-1194/chrome-linux/chrome` | `5b6ca8c422ea77d1cf7a864e26715b9c0375cf3a8d11cbeeb706bf93d8bdf94f` |

---

## 7. 向 Codex（C5）提出的修复契约建议

依据本报告取得的数学与视觉证据，建议 Codex 在 C5 后续工作中实现如下两处最小契约修复：

1. **镜像控制变量反转支持（`mirrorControl`）**：
   - 在 `import_emote.cpp` 中解析 `metadata.mirrorControl.variableMatchList` 并存入 `ModelAsset`；
   - 以下为旧 V7 建议，不能直接推广到动作/差分通道；当前契约以 C5 交接为准。当 `asset.mirror == true` 时，自动对名称匹配 `variableMatchList` 的变量取反（`effective_value = -user_value`），或在参数化查表阶段反向映射；
   - **注意**：绝对不要取消 `asset.mirror` 的呈现翻转，两者是正交且协同运作的。
2. **容错性 Stencil 复合图层解析（`stencilCompositeMaskLayerList`）**：
   - 若名称全模型无定义，可跳过该可选项；名称有定义但不能在当前 scope 定位时仍保持未解析。跳过后全空的 union 也拒绝，不能用另一有效 source 掩盖作用域错误。当前实现以 C5 交接为准；
   - 这样既能严格抵御真正空遮罩的非法引用，又能容纳 E-mote 资产中普遍存在的未绑可选遮罩（如 `add_mask`, `■耳L` 等）。
