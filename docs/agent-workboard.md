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

用户确认正式镜像必须保留。先读V6及C5失败：原生Preview已按asset.mirror翻转，但Fraise france-maid-b闭眼混合姿态仍有约57.775px几何误差；默认眼态被stencil拒绝。观察目标是定位差异，**不能把取消镜像作为修复**。

- PSB保持原始，固定800×1080、x=0/y=400、scale1、opacity1、clock1000ms/seed1、steps0、物理幅度0。分别采样face_eye_open=10的中立、head_LR±30、body_LR±30，再采样默认眼态。以实际源默认值/最终变量及asset来源说明条件；不把不同控制器/衣装/素材当同条件。
- 保存完整WebGL和现有原生截图/几何、按相同icon/UV定位的GPU顶点、MVP、a_color、blend/colorMask/FBO/遮罩draw顺序。给出X/Y/UV及黑白合成误差、局部裁切和PASS/FAIL/未测，区别镜像呈现与形变后剩余差异，不根据字段名猜公式。
- 列出source JSON和当前asset的stencil_composite_mask_layers、同/父scope中实际标签。不存在/嵌套来源只记录事实及源GPU行为，不改原生忽略缺失引用。需要额外诊断入口时写最小需求并继续可独立部分。
- 输入/工具/二进制hash先冻结；GPU以 `flock -n build/gpu-validation.lock` 串行，不覆盖旧证据，不放宽.001px/1e-5/2字节门槛，不注入系统键鼠。动态UI回放入口仍未建立，本任务不承担UI动态验收。

## Codex接续

- C5：bm19/attr2映射已交付，Milk默认启动恢复，新完整播放9/9和旧矩阵115/115通过；继续Fraise-b遮罩、受控镜像姿态与精确0x7fc，C5整体尚未完成。
- C6：恢复界面/能力提示、UI注视与触摸隔离及确定回放、线性角色渐变/共同时间、背景reset/重入、声音候选失败和资源回收。
- C7：最小Qt Quick宿主验证实际GPU后端、图形线程/context、resize/DPI与重建，再进入Plasma插件。全目录视觉/交互、8–24小时、多屏和安装包仍是最终条件。
