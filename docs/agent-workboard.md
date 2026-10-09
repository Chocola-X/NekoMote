# Agent协作任务

更新2026-10-09，当前以[摘要](review-agents-current.md)、[本轮复核](agent-review-2026-10-09.md)、[下一批交接](agent-next-handoff.md)为准。

| 任务 | 当前状态 | 下一步 |
| --- | --- | --- |
| G13/G14 | 冻结诊断目录/Qt入口已验收，不重复实现 | 沿用明确范围 |
| G17 | 独立修真实帧扫描：15FAIL、180 NOT_RERUN、234静态候选，31合成通过 | GLM G20扩展来源/准确继承证据 |
| G16 | 独立整合动态模块+实际Plasma隔离KPackage/WallpaperItem/config页；初步显示/错误/调度通过 | GLM G19主力完善 |
| G18 | 独立修严格validator/28合成，39CPU点通过含17纠正idle | GLM G21执行，不重写复杂判定 |
| V12 | 撤回认证：3 PENDING、177 UNREVIEWED、15 BLOCKED；候选不应用 | Gemini V14真实观察 |
| G19 | 已完成：生命周期/错误恢复/配置测试全通，40轮重建平衡，见 docs/g19-plasma-report.md | 沿用 |
| G20 | 候选完成：隔离worktree验证误差8.95px未过门槛，正式拒绝保持，见 docs/g20-inheritance-evidence.md | Codex复核候选与根因 |
| G21 | 已完成：独立source-field参考验证通过，moved/scaled_half 30点+边缘12点全OK，见 docs/g21-touch-handoff.md | Gemini V14视觉观察 |
| V13 | 已完成：实际包视觉与配置交互通过，QML对WebGL差0，见 docs/v13-plasma-visual-report.md | 沿用 |
| V14 | 已完成：三重点型号逐条件真实目视观察与81测点复核，3 PENDING/177 UNREVIEWED/15 BLOCKED保持，见 docs/v14-touch-report.md | 沿用 |
| C5/C6/C7 | 正式语义及声音/背景/应用/宿主最终整合未完成 | Codex风险审查；明确局部实现继续交GLM |

顺序：**GLM G19→G20→G21；Gemini V13/V14可独立取图测点，补包后验收**。GPU共享flock串行；已有锁不重复套锁，长跑冻结二进制/hash。领用声明base/文件，保护共享未提交工作。

GLM负责契约明确的复杂C++/Qt、构建、批量执行和证据；Gemini负责实际视觉，不硬编码观察或外推；Codex负责新语义、架构/ABI/线程及风险抽核。额度优势不降低质量，摘要≤一页，raw保存在新build目录。证据交付/实现完成/独立验收分开，不把Skipped计PASS。

实际桌面配置/安装、正式声音/背景、长期/多屏、完整195视觉尚未认证。用户已安装SDK；不用重复“缺SDK”或新增无依据的确认流程。外部原素材只读，输入只用自有回调/浏览器事件。
