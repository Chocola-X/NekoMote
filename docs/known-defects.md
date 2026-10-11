# 当前缺口与待验收范围

更新：2026-10-11，实现基线 `b474e19`。只列未闭合或尚未最终验收的范围，不把历史失败当当前故障。单边界0x7fc、旧15型号加载失败、Fraise镜像/可选遮罩和覆盖图540px偏移已修，不再列为待实现。G31A/G31B/G32/G30已交付：WallpaperController下沉、UI层分离、ABI扩展+Plasma薄壳迁移、ABI声音集成，170/170测试通过；Plasma QML已暴露muted属性。以下为剩余待验收范围。

| 范围 | 当前缺口 | 负责人/任务 |
| --- | --- | --- |
| 求值/精度 | 3个bust_LR20仍FAIL待诊断；未验证嵌套/其他继承语义继续拒绝；两镜像外力标量反例和非零X快照对照未闭合 | Codex C8；[正式继承](inheritance-semantics.md)、[标量原始记录](c5-mirror-mask-handoff.md) |
| 资源/线程 | 异步候选/GPU就绪/失败事务/卸载的所有权契约已统一到Controller，但未做TSan或驱动故障注入验证 | Codex抽核；后续TSan在确有需要时另做 |
| 服务与时间 | 声音已通过ABI集成（G30）；角色/背景共同暂停时间预算、reset/重入、连续切换资源回收未最终验收 | Codex复核；长期运行验收 |
| 触摸/UI | 触摸候选不应用；逐型号区域贴合、UI事件隔离、动态姿态仍需严格证据 | Gemini按共享UI与有效证据观察；G28/V19后续结果待范围复核 |
| 覆盖/性能 | 195初态通过不等于全动态；G29交付矩阵/分配/暂停报告待范围复核，locator/ASan/整应用与设备线程覆盖不自动PASS | Codex抽核G29；后续全动态/长期 |
| 图形宿主/桌面 | Plasma已迁移为薄壳（wallpaper_item 297行、main.qml 34行）；隔离包/安装提交不证明真实桌面、多屏、休眠、context失效或8–24h稳定 | 独立用户账户验收；安装/桌面配置按实际授权 |
| Vulkan后端 | application/ui已与GL解耦，公共契约只用语义ID与值类型；render_vk尚未建立，不创建空后端 | 未来P10，按需实现 |

真实素材缺失/损坏按来源记录，不以结构导入或目录通过推定画面。Coconut dress来源差异和Vanilla santa项目图集修复记录留在[C5证据](c5-mirror-mask-handoff.md)、C6与冻结账本，不再分派已完成图集重审。当前执行与验收顺序见[专项交接](wallpaper-application-handoff.md)。
