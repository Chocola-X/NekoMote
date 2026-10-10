# V19：触摸视觉核查与静态端点补验

Antigravity / Gemini 3.8 Flash，基线 `bfa8353`（G29）；消费 G28 证据包（`build/evidence/g28-touch/`）与 Codex 冻结证据。raw 保留在 `build/evidence/v19-touch-audit/`。

## 1. G28 三型号五条件逐图目视核查（15 clean + 15 overlay）

G28 彻底修复了 G24 覆盖图缺失 540px 半高偏置的 Bug（`PNG_HEIGHT // 2 - 540 == 0`）。逐图真眼审核五部位几何椭圆与角色解剖轮廓：

- **头（head，青色）/ 眼（eye，黄色）/ 脸（face，绿色）**：各变换下精确贴合发饰/猫耳、双眼与面颊下巴。
- **胸（bust，红色）**：
  - `fraise-france-maid-b`：紧密包裹衣领、铃铛与胸部。
  - `cinnamon-maid-b`：胸部存在侧倾夹角，椭圆准确覆盖倾斜胸部及 Cinnamon 爱心徽章。
  - `coconut-santa-b`：胸部饱满，抬起的右手前臂横跨胸前（存在肢体交叉遮挡），椭圆准确圈定胸口解剖区。
- **下身（pant，洋红）**：
  - `default` / `idle120`：角色居中偏上，骨盆与裙摆/短裤部分出画，但中心定位准确。
  - `lower_body_in_frame`（originY=-200）：下身完整入画，Fraise 女仆围裙、Cinnamon 裙摆与 Coconut 紫色腰带/圣诞短裤完全位于 alpha bbox 内部，`pant` 椭圆准确包裹骨盆与短裤区域。
- **变换同步**：`moved`（originX=80）全量右移 80px，`scaled_half`（scale=0.5）精准 0.5x 等比缩放，椭圆与角色无脱节或滞后；`idle120` 终态姿态自然，椭圆随骨骼微调贴合。

## 2. 证据绑定与 Ledger 执行

- 构造 version 3 观察 `build/evidence/v19-touch-audit/observations.json`，绑定 `model_asset`、`session_asset`、`source`、`reference` 及 15 组 clean/overlay/probe 路径与 SHA-256。
- 运行 `generate_v9_ledger.py --g12-ledger build/evidence/g28-touch/g12-ledger/ledger.json --g28-freeze build/evidence/g28-touch/freeze-manifest.json --observations build/evidence/v19-touch-audit/observations.json --output build/evidence/v19-touch-audit/ledger`。
- **Ledger 结果**：`{"MATCH": 0, "MISMATCH": 0, "UNREVIEWED": 0, "PENDING_EVIDENCE": 3, "BLOCKED_INPUT": 0}`。
- **诊断分析**：G12 为纯 CPU probe（`capture: null`），G28 `lower_body_in_frame` 为画幅验证无 CPU probe；按 G28 契约测试（`test_merged_condition_still_requires_evidence`），缺少跨源完整探针严格置为 `PENDING_EVIDENCE`，不凭 CPU 成功伪造全绿。
- G12 的 132 个 CPU 点击点在 `point-binding.json` 完整绑定（全部 hash 验证通过）；clean 图像与 GLFW player 对比 RMSE 为 0.011–0.015；候选区域保持 `PENDING_EVIDENCE`，严禁直接应用。

## 3. Codex 12 新型号 24 静态与 3 idle120 端点补验

对单边界 0x7fc 集合（4 Azuki B、1 Cinnamon maid、10 Coconut 中扣除代表后的 12 型号）及 3 代表 idle120 终态进行视觉与数值审计：

- **24 静态条件（default 及 body_LR30）**：最大几何误差 0.000130px（门槛 0.001px），WebGL/GPU 容差 2 零超差；手腕无断裂或 8.95px 平移错位，遮罩边缘无溢出。
- **3 代表 idle120 终态（body_LR30）**：120 步 22 物理通道最大标量 5.96e-8，最终几何 ≤ 0.001px，衣袖与手腕连续。
- **边界说明**：以上仅认证离散静态端点，不外推为连续动态动画；3 个 bust_LR20 保持 FAIL，不认定无公式反例。

Agent: Antigravity
Model: Gemini 3.8 Flash
Context: docs/agent-workboard.md V19
