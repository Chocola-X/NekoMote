# V15：触摸视觉复核状态

观察：Antigravity / Gemini 3.8 Flash，`3bb5023`，base `f7f94de`；2026-10-10 Codex复核。raw `build/evidence/v15-touch-audit/` 保留。

发现旧G24覆盖图上移约540px的目视结论有效，根因是两个覆盖坐标生成工具；Codex已修。旧idle120实际没有执行120步，lower_body未证明所有下身入画，因此不由这些图判定模型区域需修改。

`version3_observations.json` 只有表面字段，没有满足现有ledger的probe/input/hash/冻结条件契约。实际消费仍3 PENDING/177 UNREVIEWED/15历史BLOCKED，不能声称已消费为MISMATCH；历史BLOCKED不代表当前CPU加载失败。候选不应用。

新证据和逐部位观察已有[G28](g28-touch-report.md)、[V19](v19-touch-visual.md)交付报告，完整范围及ledger接受状态仍需独立复核。当前任务见[交接](agent-next-handoff.md)。
