# Agent 协作任务

当前主分支基线b11e175；核心候选d297827已隔离验证，按[摘要](review-agents-current.md)、[求值复核](codex-inheritance-review.md)、[宿主/触摸记录](agent-review-g19-g21.md)、[下一批交接](agent-next-handoff.md)执行。实现完成、证据交付、独立验收分开。

| 范围 | 实际状态 | 下一步 |
| --- | --- | --- |
| G13/G17 | 195初态180OK/15FAIL；15独立复现/234静态候选，180未重跑 | 不重复工具 |
| G19 | UI改进保留；Codex修初态重建/真实配置页、首delta0、每代40轮与最终renderer1/1 | GLM G22恢复/部署/自动实际包 |
| G20/G23 | 旧候选实际129结果、各候选未通过整体门槛；通用等价/44%/12warp推断纠正 | 见接管记录，停止改旧公式 |
| Codex求值候选 | d297827；三代表27静态几何/5GPU通过，2新增合成回归；主分支未合入 | Codex保留公式与最终放行 |
| G25 | 已完成：27静态几何+27GPU+195初态+45扩组合+性能基准全通过；交Codex复核 | Codex最终复核决定正式合入 |
| V17 | 已完成：三代表27GPU条件手腕/遮罩/动态连续性目视抽核全通，见[v17](v17-inheritance-visual.md) | 候选保持拒绝，交Codex最终放行 |
| G18/G21 | 39+42最终CPU点有效；来源缺freeze、旧summary有2修正前失败 | GLM G24新来源/Qt布局 |
| V13 | 22图hash及三QML固定图差0有效，生命周期宽泛结论撤回 | Gemini V16实际包逐条件 |
| V14 | 有有限目视依据，文件不符合version3；下身裁切仍未排除 | Gemini V15消费G24新图 |
| G22/G24 | 待领用，GLM开发与批量执行大头 | 按该顺序 |
| C5/C6/C7 | 准确语义/应用与正式宿主退出条件仍未完成 | Codex关键抽核/整合 |

GPU共享flock，不套重复锁；原素材只读；自身回调输入；声明base/文件，保留别人工作。候选正式拒绝保持，3PENDING/177UNREVIEWED/15BLOCKED保持。真实桌面/声音背景/长期/多屏/195动态视觉未认证。候选细节见[接管记录](codex-inheritance-review.md)。报告≤一页，raw保留新build目录，Skipped不计PASS。候选worktree在experiments/（.gitignore排除）。
