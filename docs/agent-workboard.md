# Agent协作任务

更新：2026-10-07。当前结论见 [整合摘要](review-agents-current.md)，阶段见 [ROADMAP](../ROADMAP.md)，新实现见 [C5交接](c5-handoff.md)。G7/G8/G9/V6已执行并复核，不重复旧任务；原handoff仅保留交付证据。本文件由用户独立启动外部Agent执行，不表示Codex已通过工具启动他们。

## 当前任务与顺序

**GLM G10 → Gemini V7 → Codex复核。** 两项文件所有权独立，可使用独立工作区并行；同一工作区及GPU证据需要串行。每项验证后commit，正文包含Agent/Model/Context，交付对应handoff和真实未通过项。

| Agent | 当前任务 | 文件边界 |
| --- | --- | --- |
| CodeArts / GLM-5.2 | G10：刷新195项CPU/来源账本；优先有明确契约的工具与负例 | 目录工具、Python测试、新g10-handoff；不改runtime/render/application/ABI/UI |
| Antigravity / Gemini 3.8 Flash | V7：镜像与遮罩的受控观察、整模型FAIL定位 | 参考采样工具、新v7报告；不改原生求值/后端/main，不读私有asm |
| Codex | C5精确继承/遮罩/镜像求值，C6应用，C7宿主整合 | 核心语义、GPU合成、声音/时间/生命周期和跨模块复核 |

额度用于独立覆盖，不作为数学或实现正确性的证明。GLM适合可执行契约和负例；Gemini适合图像与GPU观测；未知公式及整合仍由Codex负责。反复错误或指导成本超过修复成本的工作由Codex接管。

## G10：当前CPU账本

读取G7交接与C5实际HEAD，使用当前Release runtime_audit、统一catalog_index和**新的输出目录**，重新生成全部195条结构/CPU账本（建议120作者步）。记录提交、工具/hash、schema、source/resx/PNG/会话来源、每项命令与失败详情。原G7的171 CPU OK/23 FAIL是混合实现前的历史统计，不能直接复用为当前结果。

- 不覆盖assets/characters、原G7输出、冻结115组资产或外部源文件；EXISTS_UNCHECKED、歧义、缺失、CPU FAIL/TIMEOUT/ERROR一律非零。只读已有产物不等于认证，不能通过删除或自动回退绕过保护。
- 账本保留195行；visual/interaction/soak全部未测。对Fraise france-maid-b的歧义仅列候选/来源hash，不擅自挑选文件或改核心让全绿。
- 用实际负例补齐CPU详细失败报告保留、超时非零、逐项I/O错误继续账本；已有测试覆盖时不重复写镜像测试。缺素材环境的纯CLI解析测试也应执行，只有真实GPU/素材检查Skipped；不得把整类跳过误报为测试通过。
- 交付真实新导入、重复拒绝且额外文件hash不变、完整195统计及失败节点。写docs/g10-handoff.md，不改当前摘要/路线图宣称整模型通过。

## V7：受控镜像与遮罩观察

已由 Gemini 完成并交付：[V7 报告](v7-mirror-mask-report.md) 与证据目录 `build/evidence/v7/`（账本：`v7_ledger.json`）。
- **镜像姿态几何误差确证**：定位根本原因为缺少 `metadata.mirrorControl.variableMatchList`（`_LR`, `_slant`）的变量符号反转。闭眼初态（`head_LR=0, body_LR=0`）几何 0.000000000px，黑白底合成 RGB 差异 [[0,0,0], [0,0,0]]，整图 0 超差像素（PASS）；直选姿态未反转导致 81.7px / 138.0px 误差，而输入反转后（Native `head_LR=-30` vs WebGL `head_LR=+30`）几何误差骤降至 0.000191px，全图像素差为 0（PASS）；`body_LR` 同样降至 0.000266px 与 0 像素差（PASS）。C5 `head_invariance` 原 57.775px 误差反转后降至 0.000280px。保持正式镜像 `asset.mirror`。
- **默认眼态 Stencil 拒绝确证**：模型中左右眼 `ステンシル(shirome)` 声明 `["shirome", "add_mask"]`，其中 `shirome` 真实存在（左眼 icon 0065，右眼 icon 0067），而 `add_mask` 在整个源模型中不存在任何节点（属模板预留空插槽）。原生 `geometry_scene.cpp:551` 要求全项解析否则 `mask.resolved = false`，导致睁眼选中时退出码 8 拒绝。WebGL 真实 GPU 行为是将 `shirome` 绘制至 FBO 2（Attachment Texture 3）并将 alpha 复制至 G 通道（`tmp.g = tmp.a`），静默跳过不存在的 `add_mask`；瞳孔图元通过纹理单元 1 采样 G 通道完成遮罩。
- 交付 5 组 Side-by-Side 对比图、局部高倍裁切、拓扑与 GPU 流 JSON 分析及全 SHA-256 账本；未改原生核心，交接 Codex C5 闭合。


## Codex接续

- C5：bm19/attr2映射已交付，Milk默认启动恢复，新完整播放9/9和旧矩阵115/115通过；继续Fraise-b遮罩、受控镜像姿态与精确0x7fc，C5整体尚未完成。
- C6：恢复界面/能力提示、UI注视与触摸隔离及确定回放、线性角色渐变/共同时间、背景reset/重入、声音候选失败和资源回收。
- C7：最小Qt Quick宿主验证实际GPU后端、图形线程/context、resize/DPI与重建，再进入Plasma插件。全目录视觉/交互、8–24小时、多屏和安装包仍是最终条件。
