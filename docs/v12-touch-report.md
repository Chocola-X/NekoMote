# V12 触摸证据与逐姿态视觉观察报告

> Agent: Antigravity / Model: Gemini 3.8 Flash  
> Context: docs/agent-workboard.md V12  
> 日期: 2026-10-09  
> 任务基线: `b6dbaa91` (GLM G18 完成)  
> G13 冻结证据源: `build/evidence/g13-touch-audit-2026-10-08/full-catalog/`  
> G18 诊断证据源: `build/evidence/g18-touch-evidence-2026-10-08/`  
> V12 产物目录: `build/evidence/v12-touch-audit/`  

---

## 1. 任务概述与验收原则

本阶段执行协作清单指派的 **V12 任务**（重点型号可见区域逐姿态视觉观察、边缘/近空白测点与证据账本构建）。

### 核心执行原则
1. **严格 195 分母与零外推**：
   - 拒绝从历史成功或代表家族推断全目录通过。
   - 本轮仅针对完成逐姿态真实目视核验的 3 个重点型号给出状态（1 MATCH，2 MISMATCH），其余 177 个未目视核验的型号严格保留为 `UNREVIEWED`，15 个 CPU 拒绝型号保留为 `BLOCKED_INPUT`。
2. **可见性与画幅边界区分**：
   - 严格审查画幅内与画幅外区域。任何区域若不在画幅内或角色无对应绘制网格像素，明确标记为不可目视，**严禁标记五区全通（PASS）**。
3. **逐姿态证据绑定**：
   - 严格遵循 Codex 建立的 Version 3 观察 schema，每项观察的每个声明姿态必须 1:1 绑定 G13 冻结的全量产物（`clean`、`overlay`、`probe`，校验文件路径与 SHA-256）。
4. **真实测点执行与验证**：
   - 使用 GLM 交付的 `tools/touch_audit/g18_point_validator.py` 在独立进程及初态下对 39 个关键测点（中心、内边缘、外边缘、横向极限点、远空白背景）进行真实 probe 请求/响应验证，保存完整的请求响应 trace 与 SHA-256。

---

## 2. 账本统计汇总

运行 `tools/touch_audit/generate_v9_ledger.py` 生成最终账本 `build/evidence/v12-touch-audit/final-ledger/v9_touch_ledger.json`，汇总如下：

| 状态 | 数量 | 说明 |
| :--- | :---: | :--- |
| **MATCH** | **1** | `fraise-france-maid-b`（4 个可见区域严格吻合，pant 排除） |
| **MISMATCH** | **2** | `cinnamon-maid-b`, `coconut-santa-b`（胸部侧向曲面与静态椭圆不吻合） |
| **UNREVIEWED** | **177** | 其余 177 个输入有效型号，严格保留未复核状态，无脚本外推 |
| **PENDING_EVIDENCE** | **0** | 所有 3 项观察记录 0 错误通过账本校验 |
| **BLOCKED_INPUT** | **15** | 沿用 G13/G17 冻结的 CPU 拒绝型号 |
| **总计 (Denominator)** | **195** | 严格保持全目录 195 完整分母 |

候选数据状态：
- `cinnamon-maid-b`: `PENDING_EVIDENCE`（包含 5 组多姿态测点，待 Codex 最终复核）
- `coconut-santa-b`: `PENDING_EVIDENCE`（包含 5 组多姿态测点，待 Codex 最终复核）

---

## 3. 三重点型号视觉观察与测点分析

### 3.1 Fraise B (`fraise-france-maid-b`) —— 视觉状态: `MATCH`

#### 1. 姿态与条件覆盖
- **声明条件**：`default_idle`（步数 0）、`idle_120`（步数 120）、`moved`（平移 origin-x=80）、`scaled_half`（缩放 0.5）。
- **核验区域**：`["bust", "eye", "face", "head"]`（仅核验画幅内 4 个可见区域）。

#### 2. 可见区域目视核验
- **head**（头部）：中心 `[400, 455]`，半径 `[162.4, 172.6]`。准确覆盖粉发顶饰、大蝴蝶结与猫耳根部。测试点 `[400, 455]` 及顶部内边缘 `[400, 310]` 均正确触发 `fraise/h02`（phase 1）；外边缘 `[400, 260]` 正确为 miss（phase 0）。
- **eye**（眼部）：中心 `[400, 556]`，半径 `[50.8, 35.5]`。准确覆盖双眼眶。测试点 `[400, 556]` 触发 `fraise/blink`（phase 1）。
- **face**（面部）：中心 `[400, 607]`，半径 `[91.3, 81.2]`。准确覆盖下巴与脸颊。测试点 `[400, 607]` 触发 `fraise/h06`（phase 1）；侧面外边缘 `[290, 607]` 正确为 miss（phase 0）。
- **bust**（胸部）：中心 `[400, 898]`，半径 `[131.9, 101.5]`。准确覆盖胸前铃铛、领结与条纹女仆裙上沿。测试点 `[400, 898]` 及上边缘 `[400, 810]` 均正确触发 `fraise/b02`（phase 1）；外侧点 `[400, 780]` 正确为 miss（phase 0）。

#### 3. 区域重叠与优先级验证
- 坐标 `[400, 556]` 同时位于 `eye`（距离 0.0）与 `face`（距离 0.625，属于面部椭圆内部）。
- 实测派发 touch 事件后，触发动作反应为 `fraise/blink`，严格证明 **`eye` 优先级高于 `face`**。

#### 4. 下身区域（pant）边界与可见性核验（关键结论）
- 在标准画布（800×1080）下：
  - pant 中心坐标为 $Y = 1544.9$（bbox 为 $[197, 1291, 603, 1798]$），**完全在画幅之外（画幅外）**。
- 在扩展诊断画布（800×2160，`lower_body_diag`）下：
  - 虽然 pant 椭圆被放置在画幅内（中心 $[400, 2085]$，bbox $[197, 1831, 603, 2339]$），但经像素切片与 `getbbox()` 检验发现：
  - **Fraise B 角色绘制网格在 $Y = 1600$ 处完全截断**，自 $Y = 1600$ 至 $2160$ 为 $100\%$ 全透明无像素区域（RGB/A 均为 0）。
  - 作者在配置中声明的 pant 区域落在一个完全不存在角色贴图/网格的虚空空白区域。
- **结论**：任何宣称 Fraise B“五区目视通过”的结论在物理上均不成立。本次 V12 严格在 `checked_zones` 中剔除 `pant`，仅认证 4 个真正可见且有效的特征区域。

---

### 3.2 Cinnamon Maid B (`cinnamon-maid-b`) —— 视觉状态: `MISMATCH`

#### 1. 现象观察与错位机理
- **角色体态**：Cinnamon 处于 3/4 侧身视角，躯干向左微倾，左胸（画面右侧）体积向外大幅度延展。
- **作者区域定义**：作者定义的静态椭圆以中心为基准：`center: [0, -28.0]`, `radius: [132.6, 102.0]`（对应屏幕中心 $[400, 912]$，右侧边界截断于 $400 + 132.6 = 532.6$）。
- **视觉网格偏差**：通过目视核查 overlay 叠图与角色实际绘制轮廓，Cinnamon 的左胸侧向网格轮廓一直延伸到 $X \approx 560$。
- **实测验证**：
  - 中心点 $[400, 912]$：命中 `cinnamon/b02`（phase 1）。
  - 右侧内边缘 $[510, 912]$：命中 `cinnamon/b02`（phase 1）。
  - **关键测点 $[550, 912]$**：该点在画面上清晰落于女仆装胸衣边缘与胸部高光隆起处，但由于静态椭圆未向右偏移，实测响应为 **Miss（phase 0, reaction=""）**。
- **下身可见性**：pant 中心在 $Y = 1499.8$，标准画幅下完全在画幅外。

#### 2. 候选调整建议
- 建议将 bust 区域中心右移并适当放大半径：
  - `center`: `[35.0, -28.0]`
  - `radius`: `[165.0, 110.0]`
- 多姿态点测量（`default_idle` 与 `idle_120`）证实：$[150.0, -28.0]$（屏幕 $[150.0, 372.0]$）在现有配置下 Miss，在候选配置下正确 Hit。

---

### 3.3 Coconut Santa B (`coconut-santa-b`) —— 视觉状态: `MISMATCH`

#### 1. 现象观察与错位机理
- **角色体态**：Coconut 处于微侧站姿，右胸（画面左侧）前凸。
- **作者区域定义**：作者定义的静态椭圆：`center: [0, 38.0]`, `radius: [143.0, 110.0]`（对应屏幕中心 $[400, 978]$，左侧边界截断于 $400 - 143.0 = 257.0$）。
- **视觉网格偏差**：目视核对 overlay 叠图，Coconut 右胸外侧轮廓及圣诞装裹胸边缘向左伸展至 $X \approx 235 \sim 240$（对应作者坐标 $X \approx -160$）。
- **实测验证**：
  - 中心点 $[400, 978]$：命中 `coconut/b02`（phase 1）。
  - 左侧内边缘 $[280, 978]$：命中 `coconut/b02`（phase 1）。
  - **关键测点 $[240, 978]$**：该点在画面上明显落于红白圣诞服装包裹的胸部外缘，但由于静态椭圆偏向中央，实测响应为 **Miss（phase 0, reaction=""）**。
- **下身可见性**：pant 中心在 $Y = 1599.1$，标准画幅下完全在画幅外。

#### 2. 候选调整建议
- 建议将 bust 区域中心左移并适当延展：
  - `center`: `[-25.0, 38.0]`
  - `radius`: `[180.0, 115.0]`
- 多姿态点测量证实：$[-160.0, 38.0]$ 在现有配置下 Miss，在候选配置下正确 Hit。

---

## 4. 真实测点验证明细表

39 个测试点通过 `tools/touch_audit/g18_point_validator.py` 严格执行与校验，所有测试点校验结果均为 `ok=True`。

### 4.1 Fraise B 关键测点抽样

| 条件 | 屏幕坐标 (PNG) | 居中坐标 | 期望/实测 Phase | 触发反应 | 判定 |
| :--- | :---: | :---: | :---: | :---: | :---: |
| `default_idle` | `[400, 455]` | `[0, -85]` | 1 (Playing) | `fraise/h02` | Head Center Hit |
| `default_idle` | `[400, 310]` | `[0, -230]` | 1 (Playing) | `fraise/h02` | Head Inner Hit |
| `default_idle` | `[400, 260]` | `[0, -280]` | 0 (Idle) | `""` | Head Outer Miss |
| `default_idle` | `[400, 556]` | `[0, 16]` | 1 (Playing) | `fraise/blink` | Eye Hit (Priority over face) |
| `default_idle` | `[400, 607]` | `[0, 67]` | 1 (Playing) | `fraise/h06` | Face Hit |
| `default_idle` | `[290, 607]` | `[-110, 67]` | 0 (Idle) | `""` | Face Lateral Miss |
| `default_idle` | `[400, 898]` | `[0, 358]` | 1 (Playing) | `fraise/b02` | Bust Center Hit |
| `default_idle` | `[400, 810]` | `[0, 270]` | 1 (Playing) | `fraise/b02` | Bust Inner Hit |
| `default_idle` | `[400, 780]` | `[0, 240]` | 0 (Idle) | `""` | Bust Outer Miss |
| `default_idle` | `[100, 100]` | `[-300, -440]` | 0 (Idle) | `""` | Background Miss |
| `idle_120` | `[400, 898]` | `[0, 358]` | 1 (Playing) | `fraise/b02` | Bust Center Hit (idle_120) |
| `idle_120` | `[100, 100]` | `[-300, -440]` | 0 (Idle) | `""` | Background Miss (idle_120) |

### 4.2 Cinnamon Maid B 关键测点抽样

| 条件 | 屏幕坐标 (PNG) | 居中坐标 | 期望/实测 Phase | 触发反应 | 判定 |
| :--- | :---: | :---: | :---: | :---: | :---: |
| `default_idle` | `[400, 912]` | `[0, 372]` | 1 (Playing) | `cinnamon/b02` | Bust Center Hit |
| `default_idle` | `[510, 912]` | `[110, 372]` | 1 (Playing) | `cinnamon/b02` | Bust Right Inner Hit |
| `default_idle` | `[550, 912]` | `[150, 372]` | 0 (Idle) | `""` | **Bust Lateral Miss (网格上未命中)** |
| `default_idle` | `[280, 912]` | `[-120, 372]` | 1 (Playing) | `cinnamon/b02` | Bust Left Inner Hit |
| `default_idle` | `[250, 912]` | `[-150, 372]` | 0 (Idle) | `""` | Bust Left Outer Miss |
| `idle_120` | `[400, 912]` | `[0, 372]` | 1 (Playing) | `cinnamon/b02` | Bust Center Hit (idle_120) |
| `idle_120` | `[550, 912]` | `[150, 372]` | 0 (Idle) | `""` | **Bust Lateral Miss (idle_120)** |

### 4.3 Coconut Santa B 关键测点抽样

| 条件 | 屏幕坐标 (PNG) | 居中坐标 | 期望/实测 Phase | 触发反应 | 判定 |
| :--- | :---: | :---: | :---: | :---: | :---: |
| `default_idle` | `[400, 978]` | `[0, 438]` | 1 (Playing) | `coconut/b02` | Bust Center Hit |
| `default_idle` | `[280, 978]` | `[-120, 438]` | 1 (Playing) | `coconut/b02` | Bust Left Inner Hit |
| `default_idle` | `[240, 978]` | `[-160, 438]` | 0 (Idle) | `""` | **Bust Lateral Miss (网格上未命中)** |
| `default_idle` | `[520, 978]` | `[120, 438]` | 1 (Playing) | `coconut/b02` | Bust Right Inner Hit |
| `default_idle` | `[560, 978]` | `[160, 438]` | 0 (Idle) | `""` | Bust Right Outer Miss |
| `idle_120` | `[400, 978]` | `[0, 438]` | 1 (Playing) | `coconut/b02` | Bust Center Hit (idle_120) |
| `idle_120` | `[240, 978]` | `[-160, 438]` | 0 (Idle) | `""` | **Bust Lateral Miss (idle_120)** |

---

## 5. 产物交付与索引

1. **观察记录主文件**：
   - 路径：`build/evidence/v12-touch-audit/observations.json`
   - 版本：Version 3
   - SHA-256：`523fe90e1953931668e1ce2ba31f9d5045610ec1f2518e3271aee89417036d00`
2. **源码字段参考文件**：
   - `build/evidence/v12-touch-audit/references/fraise-france-maid-b.reference.json`
   - `build/evidence/v12-touch-audit/references/cinnamon-maid-b.reference.json`
   - `build/evidence/v12-touch-audit/references/coconut-santa-b.reference.json`
3. **点校验规格与执行记录**：
   - `build/evidence/v12-touch-audit/specs/*.spec.json`（39 份）
   - `build/evidence/v12-touch-audit/validated_points/*.result.json`（39 份）
4. **最终账本文件**：
   - `build/evidence/v12-touch-audit/final-ledger/v9_touch_ledger.json`
   - 校验结果：0 errors, 1 MATCH, 2 MISMATCH, 177 UNREVIEWED, 0 PENDING_EVIDENCE, 15 BLOCKED_INPUT.
5. **单元测试**：
   - `tests/reference_capture/test_v12_touch_audit.py`（7 项用例全部通过）

---

## 6. 测试与验证状态

- **Python 测试**：
  - `python3 -m unittest discover -s tests/reference_capture`：`197` 项用例（187 通过，10 显式跳过，0 失败）。
- **Release 核心 CTest**：
  - `ctest --test-dir build/release`：`145/145` 全部通过（100% passed）。
- **Plasma 宿主 CTest**：
  - `ctest --test-dir build/plasma`：`147/147` 全部通过（100% passed）。

---

## 7. 下一步建议

1. **交接 Codex 复核**：
   - Codex 可基于真实测点记录与 Version 3 账本对 `cinnamon-maid-b` 及 `coconut-santa-b` 的候选数据进行 C5/C6 语义审查。
2. **开展 V11 视觉复核**：
   - 利用 GLM-5.2 在 G16 交付的 `build/plasma/bin/nekomote_plasma_probe` 及实际 `main.qml` 壁纸包，开展 QML 壁纸包在真实/离屏环境下的视觉渲染、DPI 缩放、错误状态与生命周期核验。
