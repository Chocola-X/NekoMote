# Agent 协作任务

基线：G22/G24/G26/V15/V18后的Codex复核提交。执行[摘要](review-agents-current.md)、[复核](review-g22-g26.md)、[交接](agent-next-handoff.md)。历史raw保留；任务报告的“完成”不替代验收。

| 范围 | 当前状态 | 后续负责人/任务 |
| --- | --- | --- |
| 单边界0x7fc | 正式支持；195初态可求值，51静态及3代表idle120有参照 | Codex C8处理真实语义反例；嵌套/2标量不扩大放行 |
| G22 | 隔离包恢复/生命周期部分有效；模块安装和干净部署未通过 | GLM G27已完成（可搬移部署+独立动态宿主验证）；Gemini V20随后真实桌面 |
| G24 | 来源/42点绑定有效；旧覆盖图和idle120无效；Codex已修工具并抽核Fraise五条件 | GLM G28已完成（三型号×五条件重拍+冻结manifest+负例+Qt vs player对比）；Gemini V19逐图待执行 |
| G26 | 42静态通过/3 bust_LR20 FAIL；9动态终态几何及逐步物理有效 | GLM G29补真正暂停、locator参照、分配/ASan及动态覆盖；Codex C8诊断3 FAIL |
| V15 | 覆盖图错误发现有效，观察文件未被ledger接受；候选未认证 | V19基于G28新证据，不改模型数据 |
| V18 | 实际绑定21组终态/组合；18数值通过/3失败；所称12新型号等未绑定 | V19补有效观察清单，连续性另验 |
| 应用退出条件 | 正式声音/背景、UI/渐变/reset、195动态、长期/多屏未验收 | GLM G30在G27后推进应用功能，Codex审查服务/生命周期 |

GLM承担实现、补测与批量整理大头；遇到语义问题只挂起对应条件，继续独立任务。Gemini负责实际视觉，不用看图代替数值门槛。Codex只接管新语义、架构/生命周期与最终抽核。每阶段验证后commit并标实际Agent/Model/Context。

声明base和改动文件，保护未提交工作；GPU使用 `build/gpu-validation.lock` 串行，不向系统窗口注入输入。原仓库只读；报告≤一页，raw在新build目录，冻结argv/stdout/stderr/input/tool/binary/output。缺失/未执行不得计PASS。旧15 BLOCKED是历史视觉状态，不是当前CPU加载失败。真实用户安装须先通过G27；当前安装脚本不能作为完整部署入口。
