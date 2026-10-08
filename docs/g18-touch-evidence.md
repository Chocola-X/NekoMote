# G18：补齐真实触摸证据

> Agent: CodeArts / Model: GLM-5.2
> Context: docs/agent-workboard.md G18
> 日期: 2026-10-08
> 基线: G13 冻结证据 `build/evidence/g13-touch-audit-2026-10-08/`

## 1. 任务目标

为 Fraise B、Cinnamon maid-b、Coconut santa-b 三个重点型号准备触摸证据截图和逐点验证器，供 Gemini V12 标记测试点后由 GLM 用 probe 逐点执行。

GLM 不生成观察结论。两姿态+实际 hit/miss 证据校验后最多 READY_FOR_REVIEW，先交 Codex 复核工具，再长跑。

## 2. 交付物

### 2.1 证据截图（`build/evidence/g18-touch-evidence-2026-10-08/`）

三个型号 × 五个条件 = 15 组截图，每组包含：
- `plain.png`：全画幅截图
- `overlay.png`：带触摸区域叠加的全画幅截图
- `regions.json`：session probe 输出（含 `touch_regions`）
- `manifest.json`：完整元数据（SHA256、坐标映射、裁图信息）
- `crops/`：胸区和下身裁图（保留原坐标映射）

条件列表：

| 条件 | steps | origin | scale | 窗口 | 用途 |
| --- | --- | --- | --- | --- | --- |
| default_idle | 0 | (0,400) | 1.0 | 800×1080 | 默认初态 |
| idle_120 | 120 | (0,400) | 1.0 | 800×1080 | idle 120 步 |
| moved | 0 | (80,400) | 1.0 | 800×1080 | 平移 |
| scaled_half | 0 | (0,400) | 0.5 | 800×1080 | 缩放 |
| lower_body_diag | 0 | (0,400) | 1.0 | 800×2160 | 下身诊断布局 |

诊断布局用 2160 高度使 pant 区域可见（默认 1080 下 pant 中心在 y≈1004–1060，超出画幅）。

### 2.2 逐点验证器（`tools/touch_audit/g18_point_validator.py`）

输入：JSON spec（型号、条件、PNG 坐标、期望区域/相位/反应前缀）
输出：验证结果（实际相位/反应、区域距离、probe 返回码/hash、错误列表）

验证规则：
- 规格校验：必填字段、有限坐标、正 scale、非负整数 steps/seed、有效区域枚举
- 坐标转换：PNG → 居中输出（减半画幅，无 X 翻转）
- probe 执行：在指定 step 派发 touch 事件，advance 一步
- 结果校验：相位 ∈ 期望集、反应前缀匹配、点在期望区域内（椭圆距离 ≤ 1）
- miss 点：期望 phase=0、reaction=""

拒绝：缺字段、非有限坐标、无效枚举、probe 失败、hash 不匹配、点不在期望区域内

**不生成观察结论**，仅验证。

### 2.3 合成负例测试（`tests/reference_capture/test_g18_point_validator.py`）

16 项合成测试覆盖：
- 缺必填字段（10 个字段逐一测试）
- 非有限坐标（inf/nan/-inf）
- 无效 scale（0/负/inf/nan）
- 无效 steps/seed/reaction_seed（负/浮点/字符串）
- 无效 expected_zone（不在枚举中）
- 无效 expected_phase_in（超出 0/1/2）
- 无效 origin（空/单元素/非有限）
- 不存在的型号
- PNG→居中坐标转换正确性
- 有效规格通过校验
- 外点规格通过校验
- 全部 5 个区域枚举有效
- width/height 正数校验

## 3. 端到端验证

### 3.1 三个型号 bust 中心（default_idle）

| 型号 | PNG 坐标 | phase | reaction | 区域距离 | 结果 |
| --- | --- | --- | --- | --- | --- |
| fraise-france-maid-b | (400, 897) | 1 (Playing) | fraise/b02 | 0.005 | ✓ |
| cinnamon-maid-b | (400, 912) | 1 (Playing) | cinnamon/b02 | — | ✓ |
| coconut-santa-b | (400, 978) | 1 (Playing) | coconut/b02 | — | ✓ |

### 3.2 三个型号 pant 中心（lower_body_diag）

| 型号 | PNG 坐标 | phase | reaction | 结果 |
| --- | --- | --- | --- | --- |
| fraise-france-maid-b | (400, 2084) | 1 (Playing) | fraise/u05 | ✓ |
| cinnamon-maid-b | (400, 2039) | 1 (Playing) | cinnamon/u05 | ✓ |
| coconut-santa-b | (400, 2139) | 1 (Playing) | coconut/u05 | ✓ |

### 3.3 Fraise B 全部 5 区域（lower_body_diag）

| 区域 | phase | reaction | 距离 | 结果 |
| --- | --- | --- | --- | --- |
| bust | 1 | fraise/b02 | 0.005 | ✓ |
| eye | 1 | fraise/blink | 0.000 | ✓ |
| face | 1 | fraise/h06 | 0.009 | ✓ |
| head | 1 | fraise/h02 | 0.004 | ✓ |
| pant | 1 | fraise/u05 | 0.003 | ✓ |

### 3.4 Miss 点

远外点 (99999, 99999)：phase=0 (Idle), reaction="" ✓

### 3.5 CTest

| 构建 | 结果 |
| --- | --- |
| dev (Debug + WERROR) | 145/145 passed |
| plasma (Release + WERROR) | 147/147 passed |

## 4. Gemini V12 下一步

Gemini 需要根据实际截图标记边缘/近空白待测点：

1. 检查 `build/evidence/g18-touch-evidence-2026-10-08/<model>/<condition>/overlay.png`
2. 在每个区域边缘标记测试点（区域边界内/外）
3. 标记近空白区域和画幅外区域
4. 将标记点写入 spec JSON（PNG 坐标 + 期望区域/相位/反应前缀）
5. GLM 用 `g18_point_validator.py` 逐点执行
6. 输出：作者坐标、中心输出坐标、PNG 坐标、origin/scale/mirror/步骤/seed、请求/响应/返回码/反应/phase/hash

## 5. 未执行项

- **Gemini 标记测试点**：需要 Gemini 视觉工具，GLM 无法执行
- **逐点长跑**：待 Codex 复核验证器工具后执行
- **观察结论**：不生成，仅验证
- **V12 观察账本**：待 Gemini 填写 version3 观察
- **zone 重叠/优先级分析**：待逐点证据收集后分析

## 6. 已知限制

- 触摸是静态作者椭圆命中测试，不是变形网格命中测试
- 反应选择用种化随机，同区域多反应均匀选取
- 确定性模式禁用真实 hover/mouse
- 诊断布局用 2160 高度，pant 底部可能仍被裁切（coconut-santa-b pant bbox 到 2415）
- 裁图依赖 PIL/Pillow，不可用时跳过裁图
