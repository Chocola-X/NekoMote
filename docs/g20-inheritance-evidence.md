# G20：0x7fc 继承的准确参照与隔离候选

2026-10-09，CodeArts / GLM-5.2；base commit `c241af8`。隔离 worktree `../NekoMote-g20-candidate`（分支 `g20-inherit-candidate`），不提交生产路径。

## 1. 摘要

从 G17 的 15 FAIL 中选 Coconut-santa、Azuki-santa-b、Cinnamon-maid 三代表，提取完整腕部父子链和源字段，用 `reference_capture` 取 WebGL 独立初态参照，提出隔离数学候选并验证。

**结论：候选将几何误差从 158.91px 降至 8.95px（coconut-santa, body_LR=30），但未过 ≤0.001px 门槛。正式拒绝保持，候选不进入生产路径。剩余误差为模型相关的 X 轴刚性平移，随 body_LR 线性增长，根因未闭合。**

## 2. 三代表腕部父子链

三模型共享相同的 15 层父链（root → 0x7fc 节点），结构完全一致：

```text
■胴体セット (type=2, mask=0x20007fc)
 └── 腕R_le (type=2, mask=0x20007fc)
      └── act_sp (type=0, mscm=8, mc=0, mt=1, md=16, bp=True)
           └── act_sp2 (mscm=8, mc=1, mt=1, md=16)
                └── act_sp3 → SL → BSL → UD → LR → BUD → BLR → QUD → QLR
                     └── QLR_M (mscm=8, mc=1, mt=1, md=16)
                          └── ■腕R (type=0, mscm=0, mc=0, mt=1, md=16)
                               └── ■腕R_親変形無視 (type=2, mask=0x7fc) ← 关键节点
                                    └── 追加パーツ_leN (type=2, mask=0x20007fc)
```

**0x7fc 节点字段**：type=2, mt=0, md=0, coord=0, jt=1, mscm=0, mc=0, mesh_state=0, parameterize=state 1。

**子节点**：coconut-santa→追加パーツ_le5/le4, azuki-santa-b→追加パーツ揺れ_le4/(空), cinnamon-maid→(空)/追加パーツ_le2。

**源 JSON 路径**：
- `../NekoWebShow/model_conversion/coconut/coconut-santa/coconut-santa.json`
- `../NekoWebShow/model_conversion/azuki/azuki-santa-b/azuki-santa-b.json`
- `../NekoWebShow/model_conversion/cinnamon/cinnamon-maid/cinnamon-maid.json`

**inheritMask 位解码**（0x7fc = 0b11111111100）：
- bit 25 (inheritShape) = 0 → 不继承父形状变形
- bit 22 (inheritParent) = 0
- bit 10 (inheritOpacity) = 1 → 继承透明度
- bit 4 (inheritAngle) = 1 → 继承角度

## 3. 代码拒绝位置

`src/runtime/geometry_scene.cpp:294` — 正式拒绝 0x7fc 节点：
```cpp
n.unsupported = unsupported_common || (layer.inherit_mask != 0x20007fc &&
                   !(layer.type == 12 && layer.inherit_mask == 0x24007fc));
```

`geometry_scene.cpp:938` — curve_parent 缺陷（若拒绝被放开）：
```cpp
state.curve_parent = transform_parent.child_sync == 8 ? state.transform_parent
                                                      : transform_parent.curve_parent;
```
未检查 `n.inherit_shape`，导致 0x7fc 节点的 curve_parent 继承为 QLR_M，手部顶点被父级躯干曲面二次扭曲。

`geometry_scene.cpp:737` — deform 回溯链：
```cpp
Vec2 deform(NodeId id, Vec2 p) const noexcept {
    while (id.valid()) { if (s.transform.mesh) p = warp(s, p); id = s.curve_parent; }
    return p;
}
```

## 4. WebGL 参照证据

用 `tools/reference_capture/capture.py` 取原 PSB/WebGL 独立初态，800×1080, origin (0,100), scale 0.45, 透明背景。Chromium headless, NVIDIA RTX 3060。

| 模型 | 条件 | visiblePixels | PNG SHA-256 |
| --- | --- | --- | --- |
| coconut-santa | default | 195171 | 7484a9ea... |
| coconut-santa | body_LR=30 | 195117 | c2cc4ac0... |
| coconut-santa | body_LR=15 | 195057 | 75266efc... |
| coconut-santa | body_LR=5 | 195165 | 61f39962... |
| azuki-santa-b | default | 180776 | 32db2348... |
| azuki-santa-b | body_LR=30 | 180782 | f4eedf81... |
| cinnamon-maid | default | 136669 | 32c138aa... |
| cinnamon-maid | body_LR=30 | 137232 | 8733975f... |

完整 hash 见 `build/evidence/g20-inheritance/webgl_ref_hashes.txt`。参照源码/PSB/texture/browser 均为只读，未修改原仓库。

## 5. 隔离数学候选

### 5.1 候选公式

在 `geometry_scene.cpp` 的 `evaluate()` 中，对 `!n.inherit_shape`（0x7fc）节点：

```cpp
// 1. 阻断 curve_parent，防止子树顶点被父级曲面 warp
state.curve_parent = NodeId{};

// 2. 将节点原点映射至父级曲面挂接位置（刚性绑定）
state.world.translation =
    instance.deform(transform_parent.curve_parent, state.world.translation);
```

同时放开 0x7fc 的正式拒绝（仅隔离 worktree，不提交生产）：
```cpp
n.unsupported = unsupported_common || (layer.inherit_mask != 0x20007fc &&
                   layer.inherit_mask != 0x7fc &&
                   !(layer.type == 12 && layer.inherit_mask == 0x24007fc));
```

### 5.2 验证结果

| 模型 | 条件 | 原始误差 | 候选误差 | 门槛 | 判定 |
| --- | --- | --- | --- | --- | --- |
| coconut-santa | default | — | 0.00008px | ≤0.001px | PASS |
| coconut-santa | body_LR=5 | — | 1.47px | ≤0.001px | FAIL |
| coconut-santa | body_LR=15 | — | 4.43px | ≤0.001px | FAIL |
| coconut-santa | body_LR=30 | 158.91px | 8.95px | ≤0.001px | FAIL |
| azuki-santa-b | default | — | 0.00006px | ≤0.001px | PASS |
| azuki-santa-b | body_LR=30 | — | 1.14px | ≤0.001px | FAIL |
| cinnamon-maid | default | — | 0.00007px | ≤0.001px | PASS |
| cinnamon-maid | body_LR=30 | — | 0.10px | ≤0.001px | FAIL |

### 5.3 误差特征

- **方向**：纯 X 轴平移，Y 完全匹配。
- **分布**：误差集中在 tex/0077（8.95px）和 tex/0078（6.59px），其余 35 个 primitive 误差 <0.0001px。
- **线性性**：误差随 body_LR 线性增长（0→0px, 5→1.47px, 15→4.43px, 30→8.95px），比例 0.298px/unit。
- **模型相关**：coconut-santa 8.95px, azuki-santa-b 1.14px, cinnamon-maid 0.10px，同链结构不同误差。

### 5.4 尝试的变体

| 变体 | coconut-santa | 说明 |
| --- | --- | --- |
| v1: deform(world.translation) | 8.95px | 当前候选 |
| v2: deform(local.translation) before compose | 13.68px | 更差 |
| v3: deform(parent.world.translation) + offset | 8.95px | 与 v1 相同 |
| 不阻断 mesh_path | 8.95px | 无影响 |

## 6. 未闭合的未知

1. **剩余 ~8.95px X 偏移的根因**：候选正确阻断了 curve_parent 并映射了原点，但存在模型相关的线性 X 偏移。可能原因：
   - bezier patch 输出坐标系与世界坐标系存在未捕获的偏移
   - QLR_M 的 domain_corner/domain_inverse 计算在原点映射时有精度差异
   - E-mote 对 inheritShape==0 可能还有额外的原点空间变换步骤
2. **inheritMask 其他位的作用**：inheritParent=0 和 inheritAngle=1 的组合在差分动作叠加时是否影响局部旋转中心，缺少独立微样本证明。
3. **多变形器交互**：act_sp/BUD/BLR 有 bp 数据但不在 curve_parent 链中，它们通过 world transform 组合影响顶点。候选未改变 world transform 的旋转/缩放分量，仅修改平移。这是否正确取决于 E-mote 对 inheritAngle 的精确语义。

## 7. 正式路径状态

- 正式拒绝保持不变（`geometry_scene.cpp:294`）。
- 候选仅在隔离 worktree `g20-inherit-candidate` 中实现和验证。
- 不提交生产路径放行、型号名分支或 approx fallback。
- 交 Codex 独立复核候选公式和剩余误差根因。

## 8. 证据文件

- `build/evidence/g20-inheritance/chain_extraction.json` — 三代表完整链字段
- `build/evidence/g20-inheritance/webgl_ref/` — 8 张 WebGL 参照 PNG + JSON + trace
- `build/evidence/g20-inheritance/webgl_ref_hashes.txt` — SHA-256 hash
- `build/evidence/g20-inheritance/candidate_results/` — 候选验证结果 JSON
- 隔离 worktree: `../NekoMote-g20-candidate` (分支 `g20-inherit-candidate`)
