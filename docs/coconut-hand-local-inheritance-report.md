# Coconut 手部与局部继承（inheritMask 0x7fc）视觉与几何分析报告

- **日期**：2026-10-07
- **执行 Agent**：Antigravity
- **执行模型**：Gemini 3.8 Flash (High)
- **任务上下文**：docs/agent-workboard.md V2 / R2
- **参照基线**：WebGL 原生捕获 `build/evidence/review-2026-10-07/coconut-body30-reference.*`

---

## 1. 现象总览与受控复现条件

### 1.1 受控复现输入参数
- **模型**：`coconut-maid`
- **变量**：`body_LR = 30`
- **步数**：0 步（初始姿态）
- **渲染参数**：`origin = (0, 100)` (像素原点对应 400, 640)，`scale = 0.45`，尺寸 `800 × 1080`，透明背景
- **物理**：SDK 物理 scale 全为 0 (`hair=0, parts=0, bust=0`)

### 1.2 宏观比对数据
- **冻结参照 PNG SHA256**：`eae541b4dce78dc977eb9187d25a4d596943dc89910a356ca30d3c4c41e78a96`
- **当前原生 PNG SHA256**：`77d233281a93fa2e178fe403e4e70d80d1e2cf13fc6755e3fdeda31014f04b4f`
- **容差门槛**：`tolerance = 2`
- **超差像素数量**：**13,111 像素 (FAIL)**
- **最大几何误差**：**8.876191 px (FAIL)** (门槛 0.001 px)
- **超差像素坐标范围**：`X ∈ [273, 529], Y ∈ [742, 900]`（完全限定在双手腹前交叠区域）

---

## 2. 视觉差异深入核查

通过高分辨率裁切与差值图观察，视觉问题集中表现在双手与小臂/手腕的连接处：

### 2.1 画面右侧手（左手，`■腕L_親変形無視`）
- **参照原图**：`build/evidence/v2_analysis/hand_left_ref.png`
- **原生当前图**：`build/evidence/v2_analysis/hand_left_nat.png`
- **差异二值图**：`build/evidence/v2_analysis/hand_left_diff.png`
- **视觉现象**：
  - WebGL 参考端：手掌与蕾丝手套自然交叠，小臂到手腕肌肉线条平滑连接。
  - 原生当前端：整只手掌及蕾丝手套沿 X 轴向右偏离约 8.88 像素，导致手腕骨骼与小臂出现明显横向错位和撕裂断层。

### 2.2 画面左侧手（右手，`■腕R_親変形無視`）
- **参照原图**：`build/evidence/v2_analysis/hand_right_ref.png`
- **原生当前图**：`build/evidence/v2_analysis/hand_right_nat.png`
- **差异二值图**：`build/evidence/v2_analysis/hand_right_diff.png`
- **视觉现象**：
  - 原生当前端的手掌与小臂整体向左下方偏离（欧式距离约 6.59 px），导致手臂与躯干连接处被明显拉扯变形，袖口与小臂连接处出现明显的折线锯齿。

---

## 3. 几何与坐标误差量化（交 Codex 数值推导输入）

利用 `nekomote_pose` 针对 `coconut-geometry.json` 中的各节点进行误差排序：

| 节点层级 | 最大误差 (px) | RMS 误差 (px) | 样本顶点数 | 差异类型 |
| :--- | :--- | :--- | :--- | :--- |
| `■腕L_親変形無視/.../■追加パーツ` | **8.876191** | 8.876140 | 140 | 纯刚性平移偏离（几乎纯 X 轴偏移） |
| `■腕R_親変形無視/.../■追加パーツ` | **6.592113** | 6.592092 | 143 | 刚性平移偏离（X 轴 +6.42px, Y 轴 -1.51px） |
| 其余全部节点（躯干、头部、头发、围裙等） | **< 0.000106** | < 0.00005 | 140~150 | **全部精确匹配（十万分之一像素级）** |

### 3.1 关键挂接点坐标抽样
针对 `■腕L_親変形無視` 下的顶点进行逐点采样发现：
- 原生坐标 Native: `(-137.858368, 197.649994)`
- 参照坐标 Reference: `(-128.982239, 197.649994)`
- **Y 坐标完全重合（误差 0.000000 px）**！
- **X 坐标偏离正好为 $\Delta X = +8.876129$ px**！

### 3.2 根因定位与公式缺陷
在 `src/runtime/geometry_scene.cpp:854-857` 中：
```cpp
if (!n.inherit_shape)
{
    state.world.translation =
        instance.deform(transform_parent.curve_parent, state.world.translation);
    state.curve_parent = NodeId{};
}
```
缺陷分析：
1. `inheritMask == 0x7fc` 的图层被设定了 `inherit_shape = false`；
2. 此时 `state.world.translation` 是通过线性刚性矩阵复合 `compose(transform_parent.world, local)` 计算出来的中间点；
3. 将该点直接传入 `instance.deform(transform_parent.curve_parent, ...)` 试图“投影回曲面”是一个未闭合的启发式公式。当父级发生身体旋转扭转（`body_LR`）时，网格变形域在非局部坐标下产生非线性的累积偏移，导致挂接点位置错误地沿身体扭转切线方向发生了 8.88 像素的刚性偏航。

---

## 4. 影响范围

全目录扫描确认：
所有双手交叠于腹前、或手部作为独立追加图层（`追加パーツ`）的模型，其挂接参考节点均为：
`type = 2, inheritMask = 0x7fc, label = "■腕*_親変形無視"`。
涉及全系列型号包括：
- `coconut-maid`, `coconut-maid-a`, `coconut-maid-a-4yo`, `coconut-santa`, `coconut-wintermaid`
- `vanilla-maid*`, `vanilla-winter*`, `vanilla-yukata*`
- `maple-maid*`, `maple-winter*`, `milk-summer`, `milk-teenage*` 等数十个型号。

---

## 5. 结论与执行动作

1. **视觉核查结论**：手部拉长和错位缺陷已获得精确的高清裁切证据与像素级量化，误差完全集中在 0x7fc 挂接节点；
2. **正确性门槛恢复**：按照审查要求与 AGENTS.md 规范，在 Codex 完成真实数学公式推导与闭合前，正式路径不得继续使用有 8.87px 误差的近似公式放行。恢复对 `inheritMask = 0x7fc` 的明确拒绝（`GeometryError::UnsupportedTransform`），保持工程诚实性。
