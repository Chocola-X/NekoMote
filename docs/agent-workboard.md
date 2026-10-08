# Agent 协作任务

更新：2026-10-08。现状见 [整合摘要](review-agents-current.md)，实现与证据见 [C5 镜像/遮罩交接](c5-mirror-mask-handoff.md)。G10、V7、G11、V8 已交付并复核，不再重复领用。以下外部任务由用户启动，各任务验证后 commit，正文记录真实 Agent、Model 和 Context。

## 下一批安排

推荐 **GLM G11 → Gemini V8 → Codex 整合**。G11 和 V8 文件所有权独立；GPU 采样共享 `flock build/gpu-validation.lock`，不要与性能测量并发。额度用于覆盖实际缺口，不降低门槛。

| 执行者 | 任务 | 修改边界 |
| --- | --- | --- |
| CodeArts / GLM-5.2 | G11：195 项原始 PSB 图集与解包 PNG 来源审计 | 离线目录/采样工具、Python 测试、新 `docs/g11-handoff.md`；不改 runtime/render/application/ABI |
| Antigravity / Gemini 3.8 Flash | V8：新镜像与遮罩实现的独立画面复核 | 新截图/裁图/报告与显式输入清单；不改核心公式或外部素材 |
| Codex | C5 剩余求值、C6 应用、C7 宿主 | 语义、GPU、会话/音频/线程生命周期及最终整合 |

这些是按当前工具条件制定的分工。GLM 无图像读取能力时提供字节和来源证据，不声称目视通过；Gemini 的视觉判断必须与几何、拓扑和像素报告一起交付。反复错误或指导成本超过修复成本时由 Codex 接管。

## G11：图集来源审计

基线使用本轮 C5 提交，先读交接中的两处纹理差异。GLM 此前修复外部 Fraise 素材有用户明确授权，记录为授权修复；本任务默认只读外部素材，需要新增外部修复时记录用户对该操作的授权。

1. 用统一 `catalog_index` 保留全部 195 条目录记录；新输出目录，不覆盖旧账本、冻结矩阵或 `assets/characters`。
2. `capture.py --trace-atlas-hashes` 在公共 `texImage2D` 上传边界记录实际 RGBA8 图集的尺寸、字节数和 SHA256；与解包 PNG 解码为 RGBA8 的字节核对。捕获是诊断，不属于播放/性能路径。不要读取驱动私有函数。
3. 明确关联每个 PSB 上传与每个 resx 资源；多图集不能只取第一张。遇到非 RGBA8、无法关联、缺失或超时保留未认证状态，退出非零，不伪造零误差。
4. 账本保留 PSB/JSON/resx/PNG/工具/浏览器/binary hash、提交、实际命令、时间、逐项状态与失败详情；来源发生变化不能继续复用旧截图。Coconut dress 已确认外部 PNG 与官方上传不符，本轮隔离修正仅在项目内，不覆盖原 G10/C5 账本。
5. 工具先用合成正/负例验证关联、重复输出拒绝和错误保留，再批量执行。冻结所有工具后采样；中途修工具使用新的目录/检查点，不混称同一版本。

验收：195 行可复核来源账本、实际成功/失败/未测统计、已知两个纹理例的回归、新交接及提交。**图集一致不等于全模型视觉/交互/soak 通过。** 不重复 CPU 长跑，除非来源改变或出现新的 CPU 风险。

## V8：镜像与遮罩独立复核（已完成）

已完成独立验收与报告交付，详见 [V8 复核报告](v8-visual-report.md) 与完整账本 `build/evidence/v8/v8_ledger.json`。测试由 Gemini 3.8 Flash 独立执行，在 `flock build/gpu-validation.lock` 下完成 22 组案例验证：

- 20 / 20 标准用例全部 PASS（几何最大 $7.1976\times 10^{-5}\text{px} \le 0.001\text{px}$，拓扑全匹配，标量 $\le 10^{-5}$，超差像素全部为 0）。
- 2 个物理标量反例（`force-negative30`, `force-half-positive30`）成功复现并记录为 counterexample（几何与图像 PASS，标量最大 $1.6689\times 10^{-5}$ 及 $1.1444\times 10^{-5}$）。
- Fraise france-maid-b 默认眼态、极值 head/body `_LR`、`_slant` 与复合姿态验证直接作者输入自动取反与可选遮罩解析；Chocola date-a 验证 idle/jump 动作裁剪组隔离；Fraise maid-a 验证真实 `add_mask` 主动参与。
- 交付完整 WebGL 对比图、局部特征裁切图（眼眶、瞳孔、高光、面部轮廓、耳尖、围裙）、alpha 及黑白底合成公差报告。结论不扩大为 195 项全目录认证。

## Codex 接续

C5：剩余 15 项 CPU 失败、精确 0x7fc 与嵌套遮罩，以及新增动画组合反例。C6：可恢复错误界面、UI 注视/触摸隔离及确定回放、角色渐变/共同时间、背景 reset/重入、声音失败与切换回收。C7：最小 Qt Quick 实际 GPU 宿主的线程/context、resize/DPI、重建，随后 Plasma。全目录视觉/交互、8–24 小时、多屏与安装包仍是最终交付条件。
