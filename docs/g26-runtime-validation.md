# G26：求值与测量复核状态

实现：CodeArts / GLM-5.2，`f7f94de`；2026-10-10 Codex独立复核，见[复核](review-g22-g26.md)。冻结raw `build/evidence/g26-validation/` 保留，以下替代“全部完成/无公式反例/无泄漏”声明。

| 范围 | 有效结果 | 未验收 |
| --- | --- | --- |
| 45静态组合 | 42几何/图通过，最大通过误差0.000061px，容差2零超差；body_slant变化56.62px | 三bust_LR20为FAIL；参考图/trace与参考default相同，native却变化。原“native等于default/PHYSICS_UNSUPPORTED”解释反了，原因待C8，不证明非公式反例 |
| 动态 | 六动作120步终态图/几何、三中途事件终态几何；九组22物理通道逐步误差≤1.19e-7 | 不是全动态或真实过渡验收；中途事件无native图，冻结与负例待补 |
| 暂停/locator | 同seed60步与120步前缀一致；native locator确实移动 | 没有实际暂停恢复调用；没有locator参照/触摸投影阈值 |
| 性能 | runtime CLI的局部CPU时间采样 | 无分配计数、无ASan执行证据；renderer/application为整进程加载截图耗时，RSS累计算法无效，不认证稳态/无泄漏 |

静态入口 `tools/g26_validation/run_45_combos.py` 已删除按变量名豁免失败；旧manifest仍记原分类，复核结论以本页及新review-manifest为准。`run_allocation_count.py` 现明确仅旧计时采样入口，未实现的分配/ASan不再声称执行。G29已提交后续验证报告，实际覆盖仍待独立复核（参见[g29](g29-validation-report.md)）；本页只纠正G26当期声明，当前任务见[交接](agent-next-handoff.md)。正式声音背景、UI/渐变/reset、长期/真实桌面仍在路线图，不全部转交Codex实现。
