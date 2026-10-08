# Agent 协作任务

更新：2026-10-08。当前结论见 [整合摘要](review-agents-current.md)。
G12/V9工具已由Codex复核修补；历史195行不等于195视觉验收。
用户启动外部Agent；任务阶段验证后提交实际Agent/Model/Context，不修改外部素材/资产来绕过失败。

## 下一批顺序与所有权

**GLM：G13证据包 → G15最小Plasma/QML原型 → G14持续验收入口。**
**Gemini：V10先检查现有Fraise先行包，G13交包后扩展；G15交付后可做V11 Qt画面核对。**
Codex只负责契约、语义/生命周期风险和最终整合，完整目录执行及机械证据整理交GLM。Codex已完成C7独立Qt GPU试验，下一步实施隔离Plasma/QML原型。
GLM适合执行冻结工具、目录与来源检查，Gemini用图像工具记录实际观察；
复杂判定/验收工具已反复出错，这轮由Codex修补，不再派两Agent重写。
GPU使用共享 `build/gpu-validation.lock` 串行；工具内部已有锁时不再外套同一锁。

| 执行者 | 任务 | 修改边界 |
| --- | --- | --- |
| CodeArts / GLM-5.2 | G13：修补后全目录触摸与截图覆盖运行（已完成，dc5473b） | 新build证据、`docs/g13-touch-handoff.md`；不改工具、核心、区域数据或当前摘要 |
| Antigravity / Gemini 3.8 Flash | V10：基于真实截图/点的逐型号观察（已完成） | 新观察JSON、参考区域/命中证据、`docs/v10-touch-report.md`；不改工具、核心、资产或当前摘要 |
| CodeArts / GLM-5.2 | G14：可选Qt宿主自动验收入口和独立构建矩阵（已完成，83db3e9） | 见 [G14交接](g14-qt-host-handoff.md)，不改宿主/核心公式 |
| CodeArts / GLM-5.2 | G15：共享Qt桥、QML模块与隔离Plasma包原型（已完成，9c72813） | 见 [G15交接](g15-plasma-prototype-handoff.md)，可独立实现，禁止改求值/GPU公式 |
| Codex | C5/C6语义及应用、C7图形宿主复核（待开展） | 复杂工具维护、候选最终复核、Qt线程/context/资源及Plasma整合 |

领用声明任务/base commit/文件，保护现有未提交工作。测试通过、运行成功、目视核对和独立验收分别记录。

## G13：只运行冻结的G12工具

先读 [G12复核](g12-touch-handoff.md) 与工具help。必须使用本轮复核提交，保持历史输出。

```sh
python3 tools/touch_audit/run_touch_audit.py --reference-root ../NekoWebShow \
  --assets assets --probe build/release/bin/nekomote_session_probe \
  --player build/release/bin/nekomote_player --capture --output build/evidence/<全新G13目录>
```

保留全部195条目录及失败来源，执行完整四条件。15项已知CPU失败可能使退出非零；
明确CPU拒绝与进程崩溃、timeout、PNG缺失、hash变更，不能看最终退出码直接宣布工具成功。
逐行确认请求/响应、返回码、probe/raw sample、source/model/session/texture/tool/binary SHA可读取并匹配。
每条件同时有原图和区域覆盖图；pant等画幅外区域明确不可目视，不以CPU命中推断可见。
先Fraise B小包确认命令环境，再目录长跑，不重复图集或旧CPU时间线全审。
不重新实现判定、不根据文件名/部位名猜反应，不把缺证据降级为PASS。
交付完整目录包、状态/缺失清单与机器摘要，未测项计入分母。

## V10：真实观察与候选

先用G13冻结包；首批Fraise B胸区、Cinnamon maid-b、Coconut santa-b。
读取原图、覆盖图及至少一个动作姿态，使用source字段或WebGL公共getTouchRegions/pointInRegion
建立参考；截图存在不等于目视检查，CPU成功不等于视觉MATCH。
输入记录schema见 `tools/touch_audit/generate_v9_ledger.py` docstring：每型号observer/notes/
conditions/checked_zones及clean/overlay/probe/reference/source/model_asset/session_asset带hash证据。
现有工具用 `--observations` 接收JSON，`--output` 必须新目录，只有有效记录才计MATCH/MISMATCH。

区分坐标回归、来源估算、动作变形、优先级重叠；记录近旁空白误触/边缘漏触的实际点和反应，
独立初态避免Busy。覆盖层为静态作者椭圆，胸红/眼黄/脸绿/头青/下身紫；中心输出像素Y向下，
PNG点减半画幅，不镜像。暂停/UI拦截和画幅外不可触及分别说明。
未能确认某型号/zone/姿态就保留UNREVIEWED/PENDING，不用代表家族外推整个目录。

两旧候选没有验收：Coconut建议还包括48作者单位Y变化。新候选须匹配当前作者旧值与来源hash，
默认及另一动作姿态的实际误/漏触点支持建议，工具最多READY_FOR_REVIEW；不改assets或外部数据。
Codex复核后再统一导入/配置迁移。交付可读取的逐项观察与明确状态，不发布无证据量化断言。

## G14：Qt自动验收与构建边界

G15交付后领用；可与Gemini视觉工作独立推进，GPU仍串行。执行范围和真实用例见
[G14交接](g14-qt-host-handoff.md)。固定宿主行为已由Codex实现，GLM负责把入口接入持续检查，
不根据截图存在宣称视觉正确，不重写复杂图像评级工具。

## G15 / V11：实际包原型与视觉验收

GLM按 [G15契约](g15-plasma-prototype-handoff.md)独立实现共享Qt桥/QML模块及build内壁纸包。
不安装/重启桌面；Codex复核后另作真实Plasma接入。Gemini V11用同来源/同初态的Qt包原型
与C7/WebGL截图比较镜像、遮罩、DPI和错误显示，只认证实际看过的条件；不等同正式桌面验收。

## 节约Codex消耗与质量边界

这是实际交付驱动的分工，不是通用模型排名：G12证明GLM能建设批量工具，但失败处理/来源证据曾误判；
V9证明Gemini能取图，但未观察的型号曾被批量写MATCH。两者都可做有清晰契约的复杂任务，
禁止从“程序成功/截图存在/代表家族”推定全量质量。

- GLM为主要实现者：C++/Qt包装、构建、客观测试、批量执行、原始证据与来源账本。
  在冻结接口下独立设计局部实现；没有实际图像工具时不做视觉评级；未经参考证据不改求值公式。
- Gemini为视觉验收者，也可实现局部诊断UI/捕获工具。先确认实际图像工具；复杂求值/GL公式需
  数值和图像证据及独立复核，不靠肉眼改公式或批量外推MATCH。
- Codex处理新语义、跨模块所有权/线程/ABI及高风险最终复核。契约明确的实现先交GLM；
  直接修复成本更低或连续错误实现时才接管。不为已有完整冻结证据重复全量运行。
- 交付摘要≤一页，列任务/base commit、改动文件、验收分母/PASS/FAIL/Skipped、关键hash、
  复现入口和未知；原始长日志放build证据，不贴进对话/摘要。每阶段验证后提交准确溯源。
- Codex先检查diff、来源/退出码/计数，再按风险抽核反例和代表模型；缺失证据退回补齐。
  架构/语义仍独立复核；额度优势不降低门槛。GPU串行，文件所有权声明后再修改。

## Codex接续与Plasma时机

C7独立Qt Quick GPU合成、线程边界、resize/DPI和场景图释放重建已通过小矩阵，见
[Qt宿主](qt-quick-host.md)。不要求全部195求值先通过，现在可开始隔离WallpaperItem/QML包：
先加载/后端能力与错误显示，再真实Plasma桌面输入、背景/声音、生命周期和配置。
Qt短时1FPS及超高GLFW截图裁切保留缺口，不设全局RHI、不常态读回、不复制声音服务。
C5仍有15项CPU FAIL、精确0x7fc/嵌套、两项外力标量精度；C6仍有可恢复错误/UI隔离/稳态、
角色/背景渐变及reset、声音失败/切换回收。全目录视觉/交互、8–24小时、多屏与安装为最终交付条件。
