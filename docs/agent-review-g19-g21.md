# G19/G20/G21/V13/V14 独立复核与补修

基线82ab3b6，Codex / GPT-6（精确变体未知）。工作区开始干净；保留GLM隔离worktree未提交候选。各报告已收窄到可复核事实，删除“全部完成”及错误根因假设。

| 范围 | 结论 |
| --- | --- |
| G19 | 配置UI可保留；原40轮不查销毁、恢复首帧是假设、错误测试不恢复。补真实配置页创建、初始绑定不重复重建、containment resize、QPointer代际、旧state及最终GPU计数；截图失败失败退出。首delta0用真实完成信号，等待异常不越Qt回调。隐藏先暂停调度并排空已提交帧再测稳态 |
| G20 | 有三代表/8WebGL/6CPU候选。独立CPU重算Coconut body30误差8.945888519、missing0，仍失败。实际v3不同旧v1；Y残差存在，“纯X”撤回。正式拒绝保持；15型号仍不能加载 |
| G21 | 42最终result的request/response hash有效，加G18为81点；旧28/2汇总是修正前状态。source文件实际在g21-touch-source-field，数值成立但来源manifest/生成入口缺失 |
| V13/V14 | 22图hash/三独立QML固定对照差0有效。V14纯字典不是工具version3；2160高旧图alpha到1600，不能认定下身网格结束，标未检查。3PENDING/177UNREVIEWED/15BLOCKED及候选不应用保持 |

补修后实际Plasma KPackage宿主20位置+20缩放重建，每代旧renderer1/1，最终窗口销毁后1/1；恢复首delta0、隐藏调度暂停、resize、配置页创建和最终PNG成功。已目视Chocola最终图，方向/手部层次正常；截图是常态诊断，不宣称与固定时间完全一致；后续Loader实例未重复诊断输入隔离，不能认证40轮受控输入画面对照。源码/二进制/资产及PNG纹理before/after冻结见lifecycle-final-command.json。实际坏型号通过公开错误失败退出，没有15秒超时。

Release/WERROR构建、Qt自动入口（12子用例）与配置拒绝（9默认/1可选）通过；未本轮全量重跑核心/ASan、长期、CPU峰值或性能，不声称全应用零分配。移除每帧atomic<double>诊断写入及产品QML计数入口。实际包probe预链接native仍不能证明干净动态部署，后续G22恢复与G27部署已有交付记录，按[当前摘要](review-agents-current.md)区分实际验收；不再重复分派G22。

证据在 `build/evidence/review-g19-g21-2026-10-09/`。早期首帧超时定位初始实例被替换；冻结复测曾因hide前在途帧假失败，已补调度断言/排空再测。失败目录与日志保留，不计PASS。G20候选命令/源码/二进制hash、G21最终索引均独立保存。

当期后续阶段已有交付与复核记录；当前任务以[交接](agent-next-handoff.md)为准，不再分派G22–G24。
