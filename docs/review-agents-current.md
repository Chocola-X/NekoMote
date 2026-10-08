# 当前整合与接手摘要

更新：2026-10-08。Codex复核 `c13b394` G12、`7c055dd` V9并补修工具。
执行见 [协作清单](agent-workboard.md)，本轮复核见 [G12交接](g12-touch-handoff.md)、
[V9复核](v9-touch-report.md)。历史求值证据见 [C5](c5-mirror-mask-handoff.md)、输入见 [C6](c6-input-review-handoff.md)。

| 范围 | 当前结论 |
| --- | --- |
| G12 | 原195行180 OK/15拒绝仅为历史记录，缺原始来源与失败证据；已修失败假MISS、label猜部位、漏目录、119/120步混淆及冻结。新Fraise B四条件、44个点、8PNG通过，全目录严格新工具重跑待G13 |
| V9 | 实际20代表型号60PNG+40裁图，不能认证180型号；硬编码MATCH已删除。严格账本180 UNREVIEWED/15 BLOCKED_INPUT。两项无hash/多姿态点的候选不应用，逐行观察待V10 |
| 输入 | C6呈现方向中心framebuffer像素、Y向下，宿主不镜像；session做作者触摸逆变换。静态椭圆不跟随网格，区域细偏差尚未全目录认证 |
| 求值 | G11图集194 MATCH/1已知差异；V8标准20通过/2标量失败，22项几何/图像通过；CPU180 OK/15 FAIL沿用冻结账本，精确继承/嵌套仍待处理 |
| 宿主 | C7独立Qt Quick GPU合成、resize/DPI、释放重建及输入小矩阵通过；三条件与真实WebGL alpha/黑白合成RGB一致。渲染末尾GL错误已返回ABI，真实不完整FBO负例先红后绿。实际Plasma未验收，详见 [Qt宿主](qt-quick-host.md) |

本轮Python158项通过（G12合成35、V9合成27均包含在总数），G12/V9阶段没有新增核心实现。
既有Debug145/无显示ASan143沿用上一轮证据，不声称本轮重新执行。
全目录画面/触摸/交互、UI隔离/稳态、渐变/reset、长期资源与正式安装仍未认证。

下一批GLM G13执行已修工具，不再重写判定；Gemini V10保存实际逐项观察，不从CPU/截图存在推定PASS。
GLM按G15独立实现最小Plasma/QML原型，Codex继续C5/C6及宿主最终复核，未知型号不阻塞已验证型号的宿主试验。

C7新增可选Qt宿主；Debug及Release GPU各10用例/181断言通过，含真实GPU错误和热帧零C++分配。
默认无窗口ASan重新配置确认Qt关闭，CPU用例仍沿用既有记录。本轮未测全应用零分配或功耗。
超高GLFW底部裁切及Qt短时1FPS仍记录；正常更新复测见 [机器记录](validation/c7-qt-quick-results.json)。
下一步GLM可按G15开始隔离Plasma/QML原型，G14接入持续检查，Gemini做V10/V11真实观察；全目录和应用完善并行，不推迟到全部195通过。
