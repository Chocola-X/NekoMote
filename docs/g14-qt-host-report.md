# G14：Qt诊断宿主自动验收报告

Agent: CodeArts / GLM-5.2
Context: docs/agent-workboard.md G14
基线 HEAD: 9c72813（G15-B 交付）
报告日期: 2026-10-08

## 当前独立验收状态

原10项入口可保留，Codex本轮修复默认discover按现存binary自动启动窗口的偏离：必须显式NEKOMOTE_QT_QUICK_HOST启用。GPU捕获持共享锁，CLI负例核对具体诊断与既有输出内容保持；缺素材/Pillow明确Skipped。

新增2项纯CPU启用逻辑测试；显式Qt入口12/12通过，默认全Python180项中170通过/10Skipped。四GPU项验证生命周期/尺寸，未验证逐型号视觉，像素金标准由独立QML/WebGL三初态检查提供。

原最小Qt ABI构建矩阵记录仍保留历史证据。dev145/ASan143本轮未重新全量执行，不用其数量宣布本轮新增覆盖。

结果与下一步见[独立复核](agent-review-2026-10-08.md)、[协作清单](agent-workboard.md)；旧长日志见Git历史及build。
