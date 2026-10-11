# 当前缺口与待验收范围

更新：2026-10-11，显示补修起点 `7029eda`。G31A/G31B/G32/G30 已有源码交付，不以测试数量或模块目录推定完整架构与桌面通过。UI/背景接线修复及本轮验证范围见 [说明](plasma-display-fix.md)。单边界0x7fc、旧15型号加载失败、Fraise镜像/可选遮罩与覆盖图偏移不再列为待实现。

| 范围 | 当前缺口 | 负责人/任务 |
| --- | --- | --- |
| 求值/精度 | 3个bust_LR20仍FAIL待诊断；未验证嵌套/其他继承语义继续拒绝；两镜像外力标量反例和非零X快照对照未闭合 | Codex C8；[正式继承](inheritance-semantics.md)、[标量原始记录](c5-mirror-mask-handoff.md) |
| 资源/线程 | Controller 已有候选/就绪契约；其他 ABI 异常边界、失败事务、有效上下文销毁与设备阻塞仍待复核。未做 TSan 或驱动故障注入 | Codex抽核；后续TSan在确有需要时另做 |
| 服务与时间 | ABI 已接入声音；面板静音只改配置、未联动当前设备；Plasma 33ms 调度不响应面板 FPS；角色渐变使用墙钟，暂停/恢复与 reset/重入未闭合 | Codex复核；长期运行验收 |
| 触摸/UI | player 与 WallpaperInstance 仍分别构建面板业务；本轮 Qt 输入/绘图通过不代表所有控件有效。触摸候选不应用，逐型号区域与动态姿态待验收 | Gemini按共享UI与有效证据观察；G28/V19后续结果待范围复核 |
| 覆盖/性能 | 195初态通过不等于全动态；G29交付矩阵/分配/暂停报告待范围复核，locator/ASan/整应用与设备线程覆盖不自动PASS | Codex抽核G29；后续全动态/长期 |
| 图形宿主/桌面 | Plasma 已使用高层 ABI 与 assetsDir 配置页；用户已确认实际桌面初步接入正常；多屏、休眠、context失效及8–24h仍未验收 | 补齐生命周期与长期证据；安装/桌面配置按实际授权 |
| Vulkan 后端 | player 双栈已重写，GL/VK 共享模型与 UI 协议；Plasma / C ABI 的 Vulkan 输出、其他 GPU/真正旧版 1.0 驱动、长期与设备故障未验收 | [C11 后端契约与证据](vulkan-backend.md)；不把窗口通过推定桌面通过 |

真实素材缺失/损坏按来源记录，不以结构导入或目录通过推定画面。Coconut dress来源差异和Vanilla santa项目图集修复记录留在[C5证据](c5-mirror-mask-handoff.md)、C6与冻结账本，不再分派已完成图集重审。当前执行见 [任务交接](agent-next-handoff.md)，原专项交接保留为设计门槛。
