# 文档阅读入口

接手先读 [AGENTS](../AGENTS.md)、[当前摘要](review-agents-current.md)和 [任务清单](agent-workboard.md)。共享 Controller/UI/高层 ABI/Plasma 已有实现，Plasma 初步接入已获用户确认，player 与 Plasma 已接入双后端，当前安装入口见 [Plasma 后端](plasma-dual-backend.md)，自动构建见 [分发](plasma-distribution.md)。G31 专项交接保留为设计门槛，不继续执行旧待实现顺序。总体阶段见 [ROADMAP](../ROADMAP.md)。

| 需要的信息 | 当前入口 |
| --- | --- |
| Wayland/X11 兼容 | [C17 修复与验收](wayland-compatibility-2026-10-13.md) |
| 构建/运行 | [building](building.md)、[player](native-player.md)、[Qt宿主](qt-quick-host.md) |
| 模块/ABI/所有权 | [规范](../AGENTS.md)、[G31契约](wallpaper-application-handoff.md)、[C ABI](abi-contract.md)、[约定](conventions.md) |
| 模型资产与正式语义 | [资产格式](asset-format.md)、[字段语义](semantics.md)、[单边界继承](inheritance-semantics.md)、P3–P6语义记录 |
| 未闭合问题 | [known-defects](known-defects.md)，不使用历史报告的FAIL数量作为当前状态 |
| 优化审查/下一批 | [C13 复核](optimization-audit-2026-10-12.md)、[G34 契约](optimization-suggestions-2026-10-12.md)、[验证记录](validation/optimization-results.json) |
| 内存实测与优化 | [C14 报告](memory-footprint-2026-10-12.md)、[测量记录](validation/memory-results.json) |
| 阶段验收 | G25正式整合、G22–G26独立复核（已归档）；G27/G28/G29/V19为后续交付报告，独立复核另计 |
| 原始证据 | validation中的冻结JSON与各报告指向的ignored build/evidence；Git保存源修改和被否决方案历史 |

G/V/C报告及[开发记录](development-log.md)描述对应提交时的采样/观察/失败，不重新分派当时的“下一步”，也不扩大已通过范围。KDE长参考文档用于查API来源，不是架构或任务指令。已实现的P0–P2/G7–G11/G14–G15旧任务交接、旧提问册/P6实施计划和被替代的手部近似公式长分析已删除；完整历史可从Git查看，冻结JSON/素材/截图未改写。

有效任务只在[当前交接](agent-next-handoff.md)维护。缺失历史截图明确记为不可用；不得以链接或旧报告文字宣布已看图/已测试。
