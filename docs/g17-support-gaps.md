# G17：不可选型号的语义缺口清单

更新 2026-10-08。基线 `542263a`。G13 冻结 probe SHA `8b2e877a...`，当前 Release 二进制一致。

## 1. 执行摘要

用户发现 `coconut-hawaii-a` 不可选，要求批量整理。对 G13 冻结的 15 个 CPU FAIL 型号，使用当前 Release `nekomote_session_probe` 独立复现初态，并从源码反查拒绝条件、静态收集全部候选不支持字段/位组合。

**结论**：15 个 FAIL 的**唯一触发条件**是非标准 `inherit_mask`（`GeometryError::UnsupportedTransform`）。3 种非标准值，1 种实际触发（0x7fc 腕親変形無視），2 种静态潜在（0x200020c / 0x20003fc，type=1 叶子标记点，不在渲染祖先链上）。已验证 bm19、普通 attr2 不误列。此分类完成不等于核心支持完成。

## 2. 复现结果

probe 二进制 SHA `8b2e877a1ff075ea7a51a7b1203bfcc2a9a1700ffac37ff81549a091e32f0326`，与 G13 冻结一致。15 个 FAIL 全部 returncode=1，stderr 文本与 G13 冻结证据逐字节一致：

```
nekomote_session_probe: session=6 playback=3 animation=0 geometry=5 physics=0
```

错误码映射（`include/nekomote/application/session.hpp:34` / `runtime/playback.hpp:26` / `runtime/geometry_scene.hpp:46`）：

| 码 | 枚举 | 含义 |
| --- | --- | --- |
| session=6 | `SessionError::Playback` | 会话错误来自播放层 |
| playback=3 | `PlaybackError::Geometry` | 播放错误来自几何层 |
| geometry=5 | `GeometryError::UnsupportedTransform` | 不支持的变换 |

15 个 FAIL 型号：`azuki-maid-b`, `azuki-maid-b-custard`, `azuki-santa-b`, `azuki-wintermaid-b`, `cinnamon-maid`, `coconut-dress`, `coconut-dress-a`, `coconut-hawaii-a`, `coconut-maid`, `coconut-maid-a`, `coconut-maid-a-4yo`, `coconut-santa`, `coconut-swimsuit-a`, `coconut-wintermaid`, `coconut-yukata`。

证据：`build/evidence/g17-reproduce-2026-10-08.json`、`build/evidence/g17-audit-2026-10-08.json`。

## 3. 源码拒绝条件

`src/runtime/geometry_scene.cpp:281-295` 设置 `n.unsupported`：

```cpp
const bool unsupported_common =
    layer.coordinate != 0 ||
    (layer.mesh_transform != 0 && layer.mesh_transform != 1) ||
    (layer.mesh_combine != 0 && layer.mesh_combine != 1) ||
    (layer.mesh_sync_child_mask != 0 && layer.mesh_sync_child_mask != 1 &&
     layer.mesh_sync_child_mask != 8) ||
    (!layer.transform_order.empty() &&
     layer.transform_order != std::vector<int>{0, 3, 2, 1});

n.unsupported =
    unsupported_common || (layer.inherit_mask != 0x20007fc &&
                           !(layer.type == 12 && layer.inherit_mask == 0x24007fc));
```

`src/runtime/geometry_scene.cpp:874-880` 在求值时检查祖先链：

```cpp
if (n.has_sprites)
    for (NodeId ancestor = id; ancestor.valid(); ...)
        if (scene.nodes[ancestor.value].unsupported || ...)
            return {GeometryError::UnsupportedTransform, ancestor, {}};
```

`has_sprites` 从 type=0 的 Sprite 层向上传播到所有祖先（`:321` / `:657`）。只有 `has_sprites` 节点才遍历祖先链，因此**叶子节点（无子节点、无 sprite）的 unsupported 不会触发运行时错误**。

## 4. inherit_mask 位语义

来源：`docs/coconut-hand-stretch-defect-analysis.md` 中 MmoBuilder.cs 位映射。

| 位 | 掩码 | 字段 | 标准值 |
| --- | --- | --- | --- |
| 25 | 0x2000000 | inheritShape | 1 |
| 10 | 0x400 | inheritOpacity | 1 |
| 4 | 0x10 | inheritAngle | 1 |

标准 `0x20007fc` = bit25 + bits2-10（全继承）。stencil `0x24007fc` = 标准 + bit22（type=12 遮罩专用，已支持）。

## 5. 三个功能组

### 5.1 组 A：0x7fc 腕親変形無視（实际触发）

| 属性 | 值 |
| --- | --- |
| inherit_mask | `0x7fc` |
| 缺少位 | bit 25（inheritShape） |
| 语义 | 无视父节点形状变形 |
| 层类型 | type=2（普通层） |
| 层名 | `■腕R_親変形無視`, `■腕L_親変形無視` |
| 出现范围 | 全部 15 个 FAIL |
| 分类 | **actual_triggered** |

**渲染路径证据**：每个 FAIL 型号至少一个腕层有 sprite 后代（32-73 个 sprite frames）。后代 `has_sprites=true`，遍历祖先链时发现腕层 `unsupported=true`，返回 `UnsupportedTransform`。

各型号 sprite 后代数量：

| 型号 | R | L |
| --- | --- | --- |
| azuki-maid-b | 32 | 0 |
| azuki-maid-b-custard | 32 | 0 |
| azuki-santa-b | 41 | 0 |
| azuki-wintermaid-b | 32 | 0 |
| cinnamon-maid | 0 | 32 |
| coconut-dress | 0 | 64 |
| coconut-dress-a | 0 | 64 |
| coconut-hawaii-a | 32 | 0 |
| coconut-maid | 32 | 32 |
| coconut-maid-a | 32 | 32 |
| coconut-maid-a-4yo | 32 | 32 |
| coconut-santa | 32 | 32 |
| coconut-swimsuit-a | 32 | 0 |
| coconut-wintermaid | 32 | 32 |
| coconut-yukata | 0 | 73 |

**最小真实节点链**（azuki-maid-b）：

```
root → 全身変形基礎 → ■胴体セット → 腕R_le → ... → ■腕R_親変形無視 (type=2, inheritMask=0x7fc) → 追加パーツ_le4 (type=0, sprite)
```

**代表输入**：`{"steps":0,"random_seed":1,"reaction_seed":1,"origin":[0,400],"scale":1.0}`

**已知缺陷**：`docs/coconut-hand-stretch-defect-analysis.md` 记录手部拉伸 8.87px 误差。曾被近似接受（`docs/review-2026-10-07.md:37`），已恢复明确拒绝。公式未闭合，交 Codex C5。

**原 JSON 字段路径**（azuki-maid-b）：`/object/body_parts/motion/全身変形基礎/layer[0]/children[2]/.../children[0]`，字段 `inheritMask: 2044`（0x7fc）。其他字段（coordinate, meshTransform, meshCombine, transformOrder）全部标准。

**WebGL 后续需观测**：
- `inheritShape=0` 时子节点的世界变换矩阵（2×3 affine）
- 贝塞尔变形回溯链 `curve_parent` 是否切断
- 手部网格顶点位置 vs 父腕角度
- 固定步 steps=0, origin=[0,400], scale=1.0

### 5.2 组 B：0x200020c 部分继承标记点（静态潜在）

| 属性 | 值 |
| --- | --- |
| inherit_mask | `0x200020c` |
| 缺少位 | bits 4,5,6,7,8,10（含 inheritAngle, inheritOpacity） |
| 语义 | 部分继承：继承形状但不继承角度和透明度 |
| 层类型 | type=1（exported locator/marker） |
| 层名 | `center_bust`, `center_bust_spare`, `center_obj`, `center_obj_spare`, `center_fronthair`, `center_rearhair` |
| 出现范围 | 全部 15 个 FAIL × 6 层 = 90 层 |
| 分类 | **static_potential** |

**不触发原因**：这些层是叶子节点（children=0, sprite_frames=0），不在任何 `has_sprites` 节点的祖先链上。代码在 `:289` 识别为 `partial_locator_origin`（当 `!unsupported_common && exported_locator && inherit_mask==0x200020c`），但 `n.unsupported` 仍为 true。若未来有 sprite 后代挂载到这些层下，将变为实际触发。

**最小真实节点链**：`root → ... → center_bust (type=1, inheritMask=0x200020c, 叶子)`

**WebGL 后续需观测**：
- inheritAngle=0 时标记点角度是否跟随父节点旋转
- inheritOpacity=0 时标记点透明度是否跟随父节点
- 标记点输出坐标 vs 父节点变换

### 5.3 组 C：0x20003fc 命中区域标记点（静态潜在）

| 属性 | 值 |
| --- | --- |
| inherit_mask | `0x20003fc` |
| 缺少位 | bit 10（inheritOpacity） |
| 语义 | 继承形状和角度但不继承透明度 |
| 层类型 | type=1（exported locator/marker） |
| 层名 | `hit_bust`, `hit_body`, `hit_head` |
| 出现范围 | 12 个 FAIL × 3 层 = 36 层（除 cinnamon-maid, coconut-dress, coconut-maid） |
| 分类 | **static_potential** |

**不触发原因**：同组 B，叶子节点不在渲染祖先链上。

**最小真实节点链**：`root → ... → hit_bust (type=1, inheritMask=0x20003fc, 叶子)`

**WebGL 后续需观测**：
- inheritOpacity=0 时命中区域标记点的透明度行为
- 标记点在触摸判定中的实际使用方式

## 6. 排除项

以下已验证条件在 15 个 FAIL 中**不触发**，审计脚本确认 0 例：

| 类别 | 源码位置 | 条件 |
| --- | --- | --- |
| transform_common | `:282-287` | coordinate/mesh_transform/mesh_combine/mesh_sync |
| transform_order | `:286-287` | transform_order ≠ [0,3,2,1] |
| frame_type | `:217` | frame.type == 1 |
| frame_field | `:218-222` | zx/zy/ox/oy/flip/color/motion.time_offset |
| blend_mode | `:219` | bm ∉ {0,19} |
| attr | `:887-889` | attr ∉ {0,1,2} |
| mesh_cc | `:208` | mesh.cc present |

已验证 bm19（premultiplied blend）和普通 attr2 在 180 个 OK 型号中正常工作，不误列未实现。

## 7. 分类与历史

| 分类 | 适用 |
| --- | --- |
| 尚未实现 | 0x7fc 的 inheritShape=0 语义（组 A） |
| 静态潜在 | 0x200020c / 0x20003fc（组 B/C），存在但不在渲染路径 |
| 候选未过图像门槛 | 否——CPU 求值阶段即拒绝，非图像门槛 |
| 配置/资源错误 | 否——原 JSON 即有此值，非导入错误 |
| 真正回归 | 否——0x7fc 曾近似接受但已恢复拒绝，从未正确支持 |

0x7fc 旧版能加载的提交及被拒绝原因见 `docs/review-2026-10-07.md:37`：近似公式有 8.876px 误差和 13111 个超差像素，不运行不安全旧版来宣布 PASS。

## 8. 交付物

| 文件 | 说明 |
| --- | --- |
| `tools/g17_support_audit/reproduce_fails.py` | 15 FAIL 独立复现脚本 |
| `tools/g17_support_audit/audit_support_gaps.py` | 静态不支持条件审计脚本 |
| `tests/reference_capture/test_g17_audit.py` | 审计脚本合成测试（10 项） |
| `build/evidence/g17-reproduce-2026-10-08.json` | 复现原始记录 |
| `build/evidence/g17-audit-2026-10-08.json` | 审计原始记录 |

核心代码只读，未修改。不改源素材或资产。不推导公式、不清除 unsupported。

## 9. 未解决项

- 0x7fc 的精确 inheritShape=0 公式交 Codex C5
- 0x200020c / 0x20003fc 的部分继承语义未实现，当前不触发但未来可能
- 180 个 OK 型号沿用冻结状态，不能认证为所有动作支持
- 逐型号视觉观察交 Gemini V12
