# G26：剩余求值验收与实际模型分配

CodeArts / GLM-5.2，基线 `1e3af83`（G24 提交）。从主分支开始，公式已有正式实现，不独立改 `src/runtime/geometry_scene.cpp`。

## G26-1：45 组合参照比较

从 G25 extended-combo 恢复 15 变量组合 × 3 代表模型 = 45 组合，生成原 WebGL + 正式 native 几何/图。沿用 origin(0,100)/scale 0.45/seed 1。

| 结果 | 数量 |
| --- | --- |
| PASS（几何≤.001px + 图像零超差） | 42 |
| PHYSICS_UNSUPPORTED（bust_LR 物理控制变量） | 3 |
| FAIL | 0 |

- 最大几何误差 0.000061px（阈值 0.001），顶点/三角形完整，alpha/黑白合成 RGB≤2 零超差
- body_slant 实际核验：body_LR_slant30 vs body_LR_UD30 最大顶点差异 56.62px，确认 body_slant 有实际几何影响
- bust_LR20 差异：bust_LR 是 spring/chain control binding 变量，nekomote_pose 不实现物理控制器，native 输出等同 default。非公式反例
- SDK GetVariable 主通道不含差分，最终混合用真实几何/图验证

入口：`tools/g26_validation/run_45_combos.py`，证据：`build/evidence/g26-validation/combos-45/`

## G26-2：动作/过渡/中途事件/暂停恢复/locator

三代表 × 5 场景 = 15，全部 PASS。fixedClock1000/seed1/作者步1/物理全1。

| 场景 | 数量 | 几何 | 物理标量 |
| --- | --- | --- | --- |
| 动作（挨拶/ピョン ×120步） | 6 | ≤0.000017px | ≤1.19e-7 |
| 中途事件（idle60→挨拶60） | 3 | ≤0.000038px | ≤1.19e-7 |
| 暂停恢复（60步 vs 120步前60） | 3 | 变量差异 0 | 确定性通过 |
| locator 投影（body_LR=30 移动） | 3 | 6 active locators | 6 个移动 |

- 动作/过渡：挨拶/ピョン 时间线 120 步，native vs WebGL 几何+图像+物理标量全通过
- 中途事件：step 60 切换时间线，events 字段驱动，几何+标量通过
- 暂停恢复：同 seed 60 步 vs 120 步前 60 步变量值完全一致（确定性基础）
- locator：6 active locators（center_fronthair/rearhair/bust/bust_spare/obj/obj_spare），body_LR=30 全部移动

入口：`tools/g26_validation/run_dynamic_validation.py`，证据：`build/evidence/g26-validation/dynamic/`

## G26-3：真实模型分配计数与性能

三代表 × 三层（runtime/renderer/application），Release GCC15.1.1 WERROR/LTO。

| 层 | 模型 | P50 | P95 | P99 | RSS |
| --- | --- | --- | --- | --- | --- |
| runtime | coconut-santa | 0.385ms | 0.406ms | 0.455ms | — |
| runtime | azuki-santa-b | 0.310ms | 0.331ms | 0.377ms | — |
| runtime | cinnamon-maid | 0.293ms | 0.335ms | 0.381ms | — |
| renderer | 三模型 | 1.13–1.19s | — | — | 167–168MB |
| application | 三模型 | 0.86–0.88s | — | — | 170–172MB |

- ASan（Debug ASan+UBSan，asan preset）三模型 warmup120+samples1000 无泄漏
- evaluate+build_frame 稳态路径无分配泄漏；P50 0.29–0.39ms，nodes 457–522
- CPU: AMD Ryzen 7 5800H；GPU: Radeon；warmup=120 samples=4000

入口：`tools/g26_validation/run_allocation_count.py`，证据：`build/evidence/g26-validation/allocation/`

## 未完成

45 组合中 bust_LR/UD 物理控制变量未实现（nekomote_pose 不支持物理控制器）；组合差分（SetVariableDiff）因模型无显式 variable_sources 未测试；暂停恢复仅验证确定性，未验证从中间状态恢复；真实桌面/声音/长期/多屏/195 全动态仍在路线图。公式反例交 Codex。
