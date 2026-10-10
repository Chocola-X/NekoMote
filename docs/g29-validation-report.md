# G29：动态、locator、分配与195覆盖补验

CodeArts / GLM-5.2，base `88799e1`（G28）。pause/resume 真实调用、allocation 单元测试、195 覆盖矩阵和 bust_LR20 证据保留全部通过。

## 交付

### G29-1: 二进制路径修正

- `run_45_combos.py`、`run_allocation_count.py`、`run_dynamic_validation.py`：`build/release/bin/` → `build/plasma/bin/`

### G29-2: 真实 pause/resume 验证（`run_pause_resume_validation.py`）

用 `nekomote_plasma_package_probe --lifecycle` 在三代表型号上真实调用 pause/resume/hide/show/resize。解析 stdout 验证核心不变量：

| 不变量 | coconut-santa | azuki-santa-b | cinnamon-maid |
| --- | --- | --- | --- |
| pause confirmed | ✅ | ✅ | ✅ |
| resume first delta=0.0 | ✅ | ✅ | ✅ |
| hidden confirmed (no frames) | ✅ | ✅ | ✅ |
| show confirmed (frames resumed) | ✅ | ✅ | ✅ |
| renderer created==destroyed | ✅ | ✅ | ✅ |

60/120 步确定性前缀也通过（`nekomote_pose --animation-diagnostic --steps`）。用受控宿主时间，不注入系统输入。

### G29-3: locator 数据保留

`nekomote_pose --dump-frame` 输出 locator 位置（name/node/path/active/position）。三代表型号各 6 个 active locator。bust_LR=20 vs default 对比确认 locator 位置不变（物理控制器未应用），但几何顶点/索引变化。与 session 作者坐标的完整对照交 Gemini V19/Codex C8 逐点核实。

### G29-4: allocation 计数

Catch2 `[allocation]` 标签单元测试：553 assertions in 7 test cases 全部通过。覆盖 runtime playback 热路径零分配、spring 独立状态、application allocation。ASan preset 已配置（`CMakePresets.json` asan preset，无窗口），实际 ASan 执行需单独构建。

### G29-5: 195 覆盖矩阵

用 `nekomote_pose --asset --dump-frame` 对全部 195 型号检查结构导入和初态求值：

| 结果 | 数量 |
| --- | --- |
| OK（frame_vertices>0 && frame_indices>0） | 195 |
| FAILED | 0 |
| MISSING | 0 |

每型号输出 JSON 保留在 `build/evidence/g29-validation/matrix-195/`。

### G29-6: bust_LR20 FAIL 证据保留

三代表型号 bust_LR=20 vs default 的 `nekomote_pose --dump-frame` 输出保留在 `build/evidence/g29-validation/bust-lr20/`：

| 型号 | default vertices | bust_LR20 vertices | 变化 |
| --- | --- | --- | --- |
| coconut-santa | 2348 | 2850 | +502 |
| azuki-santa-b | 1230 | 1743 | +513 |
| cinnamon-maid | 1672 | 1767 | +95 |

native 几何确实变化（顶点/索引增加），stderr 报 "physical controllers unavailable"。参照图与 default 的关系交 Codex C8 诊断语义反例，不按字段名豁免。

## 未完成

- locator 与原 SDK/作者坐标的完整逐点对照交 V19/C8。
- ASan 无窗口 preset 的实际构建和执行需单独配置。
- 动作混合/过渡及中途事件的多步几何/图采集未在本轮完成。
- 长期 8–24h 稳定性测试待实际桌面功能稳定后串行执行。

Agent: CodeArts
Model: GLM-5.2
Context: docs/agent-workboard.md G29
