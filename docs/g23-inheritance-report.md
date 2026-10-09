# G23：0x7fc 继承候选矩阵与残差分析

执行CodeArts / GLM-5.2，base c241af8，master 2afc001。正式0x7fc拒绝保持，15型号仍不支持。

## 候选公式

| 候选 | 公式 | curve_parent |
| --- | --- | --- |
| v1 | `world.t = deform(parent.curve_parent, world.t)` | ∅ |
| v3 | `world.t = deform(parent.curve_parent, parent.world.t) + (world.t - parent.world.t)` | ∅ |
| v4 | 不变形 | ∅ |
| v5 | `world.t = deform(transform_parent, world.t)`（含直接父级warp） | ∅ |
| v6 | 不变形 | 正常传播 |

v1/v3/v5在全部27条件（3代表×{default,body_LR±5/±15/±30,body_UD30,body_SL30}）下**误差完全相同**。根因：0x7fc节点局部translation=0，故`world.t == parent.world.t`，三公式数学等价。v5的■腕R warp恒等（不改变输入点），与v1/v3一致。

## 完整矩阵（max_error_px，阈值≤0.001）

| 条件 | v1=v3=v5 | v4 | v6 |
| --- | --- | --- | --- |
| coconut body_LR=30 | **8.946** | 16.106 | 160.190 |
| coconut body_LR=15 | **4.432** | 8.012 | 80.475 |
| coconut body_LR=5 | **1.468** | 2.662 | 26.910 |
| coconut body_LR=-5 | **1.512** | 2.714 | 27.142 |
| coconut body_LR=-15 | **4.662** | 8.269 | 85.817 |
| coconut body_LR=-30 | **9.726** | 16.941 | 185.393 |
| coconut body_UD=30 | **7.401** | 14.476 | 342.683 |
| coconut body_SL=30 | 0.0001 | 0.0001 | 0.0001 |
| coconut default | 0.0001 | 0.0001 | 0.0001 |
| azuki body_LR=30 | **1.144** | 0.404 | 3.736 |
| azuki body_LR=15 | **0.577** | 0.197 | 1.873 |
| azuki body_LR=5 | **0.193** | 0.065 | 0.625 |
| azuki body_LR=-5 | **0.701** | 1.674 | 1.282 |
| azuki body_LR=-15 | **2.106** | 5.023 | 4.031 |
| azuki body_LR=-30 | **4.218** | 10.051 | 8.627 |
| azuki body_UD=30 | **4.575** | 9.727 | 4.401 |
| azuki body_SL=30 | 0.0001 | 0.0001 | 0.0001 |
| azuki default | 0.0001 | 0.0001 | 0.0001 |
| cinnamon body_LR=30 | **0.101** | 3.436 | 5.263 |
| cinnamon body_LR=15 | **0.065** | 1.732 | 2.597 |
| cinnamon body_LR=5 | **0.025** | 0.580 | 0.858 |
| cinnamon body_LR=-5 | **0.012** | 0.018 | 0.404 |
| cinnamon body_LR=-15 | **0.030** | 0.052 | 1.223 |
| cinnamon body_LR=-30 | **0.045** | 0.099 | 2.482 |
| cinnamon body_UD=30 | **0.028** | 4.977 | 1.977 |
| cinnamon body_SL=30 | 0.0001 | 0.0001 | 0.0001 |
| cinnamon default | 0.0001 | 0.0001 | 0.0001 |

## 残差分析

coconut body_LR=30残差顶点（tex/0077, node=391）：native X比reference小8.946px，Y完全匹配。**残差是纯X方向常数偏移**，非非线性变形。

诊断导出（0x7fc节点evaluate）：
- node=354(右腕): world=(103.5,134.65), parent_world=(103.5,134.65), deformed=(109.38,135.68), deform_delta=(5.88,1.03)
- node=380(左腕): world=(-101.25,126.1), parent_world=(-101.25,126.1), deformed=(-94.09,126.1), deform_delta=(7.16,~0)

**world.t == parent.world.t**（局部translation=0），确认v1=v3等价性根因。deform链从QLR_M开始，经12个mesh节点（QLR_M→QLR→QUD→BLR→BUD→LR→UD→BSL→SL→act_sp3→act_sp2→act_sp）。

deform贡献7.16px修正，但需14.83px（v4误差16.11 - v1误差8.95 = deform贡献7.16）。**deform仅修正所需量的~44%**。

## 关键发现

1. **v1=v3=v5**：三公式数学等价，因0x7fc局部translation=0
2. **v6巨大误差**（coconut 185px）：0x7fc子部件顶点不能做完整curve_parent链deform，排除"保留curve_parent"方案
3. **v4误差更大**：deform是需要的，方向正确但幅度不足44%
4. **body_slant不触发**：所有候选误差~0，body_slant不影响0x7fc节点
5. **误差与body_LR近似线性**：coconut 5→1.47, 15→4.43, 30→8.95（比例~0.298），系统性偏移
6. **负值误差略大于正值**：非线性分量存在，撤回旧"纯X/对称"假设

## 结论

v1/v3是最稳定候选，但**均未过≤0.001px门槛**。0x7fc正确语义仍未闭合：deform方向正确但幅度不足，缺失变形在deform链中或输入坐标空间不正确。正式拒绝保持，不能宣称15型号可用。

证据在`build/evidence/g23-inheritance/`（manifest.json, matrix_results/）。候选源码在`experiments/g20-{v1,v3,v4,v5,v6}/`（.gitignore排除）。交Codex独立验公式/代表再整合。

Agent: CodeArts
Model: GLM-5.2
Context: docs/agent-next-handoff.md G23
