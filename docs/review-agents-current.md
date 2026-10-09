# 当前整合与接手摘要

更新2026-10-09，复核基线4552236。当前只按[路线图](../ROADMAP.md)、[协作清单](agent-workboard.md)、[下一批交接](agent-next-handoff.md)执行，旧报告溯源。

| 范围 | 当前结论 |
| --- | --- |
| 目录/求值 | G13 195条：180初态诊断OK/15FAIL沿用。修G17真实帧解包；15独立FAIL复现与234静态候选有冻结来源，180 NOT_RERUN。G20三代表0x7fc候选误差8.95px未过≤0.001px门槛，正式拒绝保持，待Codex复核根因 |
| 触摸 | G18严格重跑39/39+G21独立source-field参考42/42=81点全OK（含moved/scaled_half/边缘）。静态椭圆不跟随网格，候选不应用 |
| 视觉 | V14三重点型号逐条件目视观察完成：3 PENDING/177 UNREVIEWED/15 BLOCKED保持。Fraise/Cinnamon head-eye-face-bust VISIBLE，pant OUTSIDE_FRAME/OCCLUDED；Coconut bust OCCLUDED（手臂遮挡） |
| Qt/Plasma | G19动态模块+真正WallpaperItem/config/schema/重建完成；V13实际KPackage视觉通过，QML对WebGL差0，配置交互/错误恢复/40轮生命周期稳定 |

正式镜像保持；宿主输入为中心framebuffer像素/Y向下，不预镜像。图集194/1、V8 20标准通过/2标量失败与22几何图像通过沿用旧证据。
本轮Release构建通过、dev测试145/145全通过；Python239执行229通过/10Skipped；Qt12通过；ASan配置无Qt/Plasma/窗口。未本轮全量重跑ASan143，未测长期资源/功耗/全应用零分配。

G19/G20/G21/V13/V14全部完成。下一步：Codex复核G20候选根因；C5/C6/C7正式语义及声音/背景/应用/宿主最终整合。安装真实桌面、完整声音/背景、UI/渐变/reset、195视觉与8–24小时/多屏仍未认证。
详见[本轮复核](agent-review-2026-10-09.md)。
