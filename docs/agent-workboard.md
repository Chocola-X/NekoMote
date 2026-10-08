# Agent 协作任务

更新：2026-10-08。当前结论见 [整合摘要](review-agents-current.md)。
G12/V9工具已由Codex复核修补；历史195行不等于195视觉验收。
用户启动外部Agent；任务阶段验证后提交实际Agent/Model/Context，不修改外部素材/资产来绕过失败。

## 下一批顺序与所有权

**GLM G13 → Gemini V10 → Codex复核**。Codex可独立并行推进C7 Qt Quick/Plasma探索。
GLM适合执行冻结工具、目录与来源检查，Gemini用图像工具记录实际观察；
复杂判定/验收工具已反复出错，这轮由Codex修补，不再派两Agent重写。
GPU使用共享 `build/gpu-validation.lock` 串行；工具内部已有锁时不再外套同一锁。

| 执行者 | 任务 | 修改边界 |
| --- | --- | --- |
| CodeArts / GLM-5.2 | G13：修补后全目录触摸与截图覆盖运行 | 新build证据、`docs/g13-touch-handoff.md`；不改工具、核心、区域数据或当前摘要 |
| Antigravity / Gemini 3.8 Flash | V10：基于真实截图/点的逐型号观察 | 新观察JSON、参考区域/命中证据、`docs/v10-touch-report.md`；不改工具、核心、资产或当前摘要 |
| Codex | C5/C6语义及应用、C7图形宿主 | 复杂工具维护、候选最终复核、Qt线程/context/资源及Plasma整合 |

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

## Codex接续与Plasma时机

C7现在可用已验证模型做独立Qt Quick FBO试验，不要求全部195求值先通过；
先GPU合成、图形线程/context、resize/DPI、暂停与场景图释放重建，再实际Plasma包。
不改变全局RHI或桌面配置，不常态读回图像，不复制声音服务。
C5仍有15项CPU FAIL、精确0x7fc/嵌套、两项外力标量精度；C6仍有可恢复错误/UI隔离/稳态、
角色/背景渐变及reset、声音失败/切换回收。全目录视觉/交互、8–24小时、多屏与安装为最终交付条件。
