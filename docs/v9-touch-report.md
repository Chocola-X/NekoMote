# V9 触摸区域视觉证据复核

更新：2026-10-08。原交付 Antigravity / Gemini 3.8 Flash，提交 `7c055dd`；
Codex 独立复核后不接受“全目录视觉核对完成”。没有修改模型或区域数据。

## 实际范围

历史 `build/evidence/v9-touch-review/` 保留：20个代表型号，各有默认原图、覆盖图、
60步待机覆盖图，共60张PNG及40张头/胸裁图。另160个可运行型号没有逐项截图。
原生成器从G12非CPU拒绝状态直接写MATCH、坐标验证和优先级通过，没有读取观察记录。
因此178 MATCH / 2侧身局限与180项全部核对的结论撤回；有截图也不等于已完成五区视觉验收。
来源分类与家族计数、呼吸位移上限、WebGL完全一致、任意空白/边缘均正确等声明缺少对应证据。

坐标链修复由C6独立契约与实际测试支持；不能把用户报告的全部细微偏差归为这一原因。
新G12 Fraise B四条件仅认证投影、中心/内部点及远外点；边缘、近旁空白和视觉贴合需另记录。

严格重新分类账本：
`build/evidence/g12-v9-review-2026-10-08/v9-reclassified-final/v9_touch_ledger.json`。
完整195分母，180 UNREVIEWED / 15 BLOCKED_INPUT，0 MATCH，退出1符合未验收。
这是证据状态纠正，不表示180型号无法渲染。

## 候选数据

原 `candidate_touch_adjustments.json` 两项保留为历史，不应用：
缺source hash、多姿态实际点与参考依据，报告旧值还与候选文件不符。
Cinnamon实际旧胸区center=[0,-28],radius=[132.6,102]；Coconut旧center=[0,38],radius=[143,110]。
Coconut建议Y=-10包含48作者单位上移，不能当作已验证的小量侧向修正。
重新建议须核对当前session旧值、source/model/session SHA及默认与另一动作姿态，记录误触/漏触点。

## 工具与下一轮

`generate_v9_ledger.py` 现在只接受显式逐型号观察记录，校验原图/覆盖图/probe/参考、
源/模型/会话hash并绑定G12冻结行。无观察为UNREVIEWED，缺据为PENDING_EVIDENCE；
有效状态只认证列出的姿态与区域。参考可为WebGL公共区域/命中输出或可复核源字段比较。
候选最多READY_FOR_REVIEW，不自动应用。新输出拒绝覆盖。
`v9_visual_capture.py` 保存命令/返回码/PNG/日志/纹理hash及部分失败状态，只表示CAPTURED，
不生成视觉PASS。27项真正合成分支测试通过。

先GLM G13产出冻结目录包，再Gemini V10保存实际观察，优先Fraise B胸区及两侧身候选。
执行命令、输入schema和证据要求见工具help/docstring与 [协作清单](agent-workboard.md)。
只用应用/页面内事件，不向系统注入键鼠。不要修改外部素材或直接覆盖当前区域。
