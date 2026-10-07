# 当前缺口

更新：2026-10-08。已闭合的默认 Milk 启动、Fraise B 缺失可选遮罩、镜像姿态及独立高光裁剪不再列为待办。验证边界见 [当前摘要](review-agents-current.md) 和 [C5 交接](c5-mirror-mask-handoff.md)。

| 范围 | 缺口 | 所有者 |
| --- | --- | --- |
| 求值 | 15 项 CPU FAIL：4 个 Azuki B、Cinnamon maid 和 10 个 Coconut；精确 0x7fc、嵌套遮罩等仍拒绝，不恢复旧近似 | Codex C5 |
| 数值精度 | 新增镜像角色缩放/外力组合有两个标量略超 1e-5 的反例；几何/拓扑通过不代替标量门槛。非零 X 物理快照的镜像源实验待补 | Codex C5 |
| 启动/恢复 | 不支持角色仍退出，缺可恢复界面、目录能力标记及明确未知角色选择行为 | Codex C6 |
| UI | 确定性动态回放、标题/淡出画面对照与注视/触摸隔离未闭合；字体/图元稳态分配未测 | Codex C6，Gemini 后续视觉任务 |
| 会话/资源 | 角色渐变共同时间、线性合成、背景 reset/重入、声音失败及连续切换回收待验收 | Codex C6 |
| 素材来源 | Coconut dress 的外部解包 PNG 与原始 PSB 公开上传不符；当前项目使用已核实旧图集。全目录来源审计仍待补 | GLM G11 |
| 覆盖/性能 | 180 CPU OK 不认证画面；195 项视觉/交互、整机同条件资源/功耗与 8–24 小时运行未认证 | P7/P9 |
| 桌面/发布 | Qt Quick 实际 GPU 宿主、Plasma、多屏卸载/重建及安装/卸载未验收 | Codex C7，P9/P10 |

15 个 CPU FAIL：

```text
azuki-maid-b, azuki-maid-b-custard, azuki-santa-b, azuki-wintermaid-b,
cinnamon-maid, coconut-dress, coconut-dress-a, coconut-hawaii-a,
coconut-maid, coconut-maid-a, coconut-maid-a-4yo, coconut-santa,
coconut-swimsuit-a, coconut-wintermaid, coconut-yukata
```

Coconut dress 与 Vanilla santa 的图集归因见 C5 交接：前者保留项目内正确纹理，后者已更新项目内错误纹理。两项均以解码 RGBA 字节对比官方上传，不能用旧背景混用的“93%差异”判断模型。缺失/失败型号保留在 195 分母中，不把结构成功、正式拒绝或近似画面视为完成。
