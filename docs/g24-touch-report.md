# G24：触摸证据复核状态

实现：CodeArts / GLM-5.2，`1e3af83`；2026-10-10 Codex独立复核，见[复核](review-g22-g26.md)。旧raw `build/evidence/g24-touch/` 保留。

三型号来源pixelMarker/presentation重新计算并与session匹配，42点CPU请求/响应绑定有效。旧36项freeze检查没有检出截图步数和覆盖图坐标错误，不能据此验收视觉。

旧15 clean/overlay存在两项缺陷：default/idle120图相同，固定步在首帧后配置而未执行120步；两个生成工具的Y换算少了窗口半高540px。lower_body条件也不证明全部下身/区域入画。这是证据工具问题，不是模型数据MISMATCH。

Codex已修工具/线程边界并重拍Fraise五条件，初态/idle120对WebGL和player通过，executed120确认；新raw `build/evidence/codex-review-g22-g26/`。后续新证据和观察已有[G28](g28-touch-report.md)、[V19](v19-touch-visual.md)交付报告，完整三型号范围、步数freeze及ledger接受状态待独立复核。触摸候选不应用。
