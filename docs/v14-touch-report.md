# V14：三重点型号真实触摸视觉观察与测点复核报告

基线：`175af57`（GLM G21）；执行：Antigravity / Gemini 3.8 Flash；证据：`build/evidence/v14-touch-audit/version3_condition_evidence.json` 与 `build/evidence/g21-touch/`。依据 [AGENTS.md](../AGENTS.md) 与 [下一批交接](agent-next-handoff.md)，本轮基于 G21 交付的独立 source-field 参考及 42 个真实测点，对三重点型号执行图像工具目视观察与逐条件状态审定，严格纠偏历史外推。

## 1. 审核结论与目录账本状态

- **195 目录账本严格沿用**：**3 PENDING_EVIDENCE**（三重点型号），**177 UNREVIEWED**，**15 BLOCKED_INPUT**。不根据单点测试或家族关系自动外推为 MATCH，不应用未经验证的候选建议。
- **坐标系与独立来源闭环**：确认直接从原始 E-mote `pixelMarker` + `cutoffY` 规范化计算，Y 轴向下无反转。Fraise 胸部中心输出 $Y=357.5$，PNG $Y=897.5$（$357.5 + 540$），与 G18/G21 实测坐标系严格一致。
- **真实测点全量覆盖**：G18（39 点，含 17 个 idle_120 于第 121 步事件）与 G21（42 点，含 moved、scaled_half 及 X250/X300/X550 边缘点）共 **81 个独立 CPU 测点**全部具有完整 request/response 及 probe 日志，100% 通过判定。
- **视觉判定客观分类**：逐条件区分为可见 (`VISIBLE`)、被遮挡 (`OCCLUDED`)、画幅外 (`OUTSIDE_FRAME`) 与未检查 (`UNCHECKED`)，明确区分模型网格截断与视口裁剪。

## 2. 三重点型号逐条件视觉证据（Version 3）

| 模型 | 条件 | head | eye | face | bust | pant |
| --- | --- | --- | --- | --- | --- | --- |
| **fraise-france-maid-b** | `default_idle` | VISIBLE | VISIBLE | VISIBLE | VISIBLE | OUTSIDE_FRAME |
| | `idle_120` | VISIBLE | VISIBLE | VISIBLE | VISIBLE | OUTSIDE_FRAME |
| | `moved` | VISIBLE | VISIBLE | VISIBLE | VISIBLE | OUTSIDE_FRAME |
| | `scaled_half` | VISIBLE | VISIBLE | VISIBLE | VISIBLE | OUTSIDE_FRAME |
| | `lower_body_diag` | VISIBLE | VISIBLE | VISIBLE | VISIBLE | OCCLUDED (0 drawn px) |
| **cinnamon-maid-b** | `default_idle` | VISIBLE | VISIBLE | VISIBLE | VISIBLE | OUTSIDE_FRAME |
| | `idle_120` | VISIBLE | VISIBLE | VISIBLE | VISIBLE | OUTSIDE_FRAME |
| | `moved` | VISIBLE | VISIBLE | VISIBLE | VISIBLE | OUTSIDE_FRAME |
| | `scaled_half` | VISIBLE | VISIBLE | VISIBLE | VISIBLE | OUTSIDE_FRAME |
| | `lower_body_diag` | VISIBLE | VISIBLE | VISIBLE | VISIBLE | OCCLUDED (0 drawn px) |
| **coconut-santa-b** | `default_idle` | VISIBLE | VISIBLE | VISIBLE | OCCLUDED | OUTSIDE_FRAME |
| | `idle_120` | VISIBLE | VISIBLE | VISIBLE | OCCLUDED | OUTSIDE_FRAME |
| | `moved` | VISIBLE | VISIBLE | VISIBLE | OCCLUDED | OUTSIDE_FRAME |
| | `scaled_half` | VISIBLE | VISIBLE | VISIBLE | OCCLUDED | OUTSIDE_FRAME |
| | `lower_body_diag` | VISIBLE | VISIBLE | VISIBLE | OCCLUDED | OCCLUDED (0 drawn px) |

## 3. 区域与网格轮廓目视特征分析

1. **Fraise (`fraise-france-maid-b`)**：
   - 姿态呈正面对称站立。`head`、`eye`、`face`、`bust` 四个可见区域与静态椭圆贴合紧密。`eye` 优先级高于 `face`，实测优先触发眨眼反应。
   - `pant` 区域中心在 $Y=1004.88$（PNG $Y=1544.88$），在常规 800×1080 视口下完全在屏幕下缘之外（`OUTSIDE_FRAME`）；在 2160 高视口诊断布局下，模型本身在腰部 `cutoffY=1134.55` 处切断，椭圆内无绘制像素（0 drawn px），故不作为有效可点击区域认证。
2. **Cinnamon (`cinnamon-maid-b`)**：
   - 角色躯干向右微倾，胸部网格发生不对称立体拉伸，右胸（画面右侧）外凸至 $X \approx 650$，而静态对称椭圆对称居中于 $X=400$。
   - 测点实测验证：$X=250$ 点落入胸部左侧空白处判定为 miss，而 $X=300$ 与内偏点判定为 hit。静态椭圆虽能捕捉大部分胸部，但与动态网格轮廓存在明显边缘偏离（MISMATCH 表现，保持 PENDING）。
3. **Coconut (`coconut-santa-b`)**：
   - 角色右手抬至下颌，手臂与红白圣诞袖筒横贯胸部正中央，导致胸部大部处于手臂图层后方（`OCCLUDED`）。
   - 下颌与嘴唇部分被手指遮盖；胸部轮廓向画面左侧大幅倾斜，静态椭圆中心落于手臂处，无法准确对应真实胸部网格。维持 PENDING，不盲目调参。

## 4. 测点执行与证据清单

- **G18 冻结测点（39 点）**：`build/evidence/g18-reviewed-2026-10-09/summary.json`，涵盖三模型默认与 idle_120（第 121 步触发），100% OK。
- **G21 补充测点（42 点）**：`build/evidence/g21-touch/validated/`，涵盖 moved（15 点）、scaled_half（15 点）、边缘 $X \in \{250, 300, 550\}$（9 点）与 bust 内偏 30%（3 点），100% OK。
- **观察证据产物**：`build/evidence/v14-touch-audit/version3_condition_evidence.json`，记录 3 模型 × 5 条件 × 5 区域的精确像素覆盖率与判定依据。
- **工具链哈希一致性**：`g18_point_validator.py` (`506bcfa0...`)、`nekomote_session_probe` (`8b2e877a...`)、`ledger.json` (`60153731...`) 均与 G18 冻结哈希一致。

## 5. 后续边界

本报告不修改核心代码、导入工具或资产数据。候选修正建议作为调试依据留存，待网格绑定与求值语义闭环后再行集成。
