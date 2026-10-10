# 下一批独立任务

基线为本轮Codex G22/G24/G26复核提交，见[复核](review-g22-g26.md)。GLM承担开发与批量运行大头，Gemini实际看图，Codex集中语义/生命周期与最终抽核。先声明base/文件；原素材只读，输入只用应用回调；GPU用 `build/gpu-validation.lock`。每阶段验证后commit，真实Agent/Model/Context；报告≤一页，raw放新build目录，冻结输入/工具/二进制/命令/日志/输出。失败只挂起对应条件，继续独立任务。

## GLM G27：完整、可搬移的Plasma部署（已完成）

报告见 `docs/g27-plasma-deploy-report.md`。CMake install()+相对RPATH+独立动态宿主验证全部通过。下一步交Codex复核部署/服务边界，Gemini V20在真实桌面观察。

## GLM G28 → Gemini V19：重建可消费的触摸证据（GLM部分已完成）

报告见 `docs/g28-touch-report.md`。三型号×五条件重拍、69/69 冻结检查、132 点绑定、30 负例测试、Qt vs player RMSE 对比全部通过。证据包和空观察模板在 `build/evidence/g28-touch/`，交 Gemini V19 逐图观察。

Gemini V19：逐图检查五部位边界、镜像/坐标/透明遮罩及真实点击点；发现区域偏差先记录区域与偏差/证据，不直接改session或模型。填写实际验证器可消费观察，运行ledger证明被接受。补V18未绑定的12型号24静态与3idle120观察（已有Codex冻结证据，不需盲重拍），不要把终态当连续性或认定bust差异无公式反例。

## GLM G29：动态、locator、分配与195覆盖补验（已完成）

报告见 `docs/g29-validation-report.md`。pause/resume 真实调用验证（三型号 OK）、allocation 单元测试（553 assertions/7 cases）、195 覆盖矩阵（195/195 OK）和 bust_LR20 FAIL 证据保留全部通过。locator 完整对照和 ASan 构建交 V19/C8。

## GLM G30：正式壁纸应用服务（G27后可开始）

先阅读`docs/abi-contract.md`和application现有声音/背景服务，提出最小契约及改动文件交Codex复核，然后实现。正式Plasma不得复制PCM/线程/反应策略；建立共享服务与宿主资源销毁/暂停/失败契约。按声音/背景→UI/角色与背景渐变/reset分别提交，验证音频设备失败、静音/卸载、暂停250ms共同预算、失败事务与恢复，不为旧ABI留兼容层。视觉交Gemini；实际桌面、长期与195覆盖不是这些单项的自动PASS。

## Codex C8 / Gemini V20

C8：先诊断三bust反例的SDK输入/绑定与native实际语义，未确认原因不改正式公式；剩余真实嵌套/2标量反例保持隔离拒绝并建立最小复现。审G27部署/服务边界、G28证据契约、G29抽核与最终整合。

V20：G27通过后观察搬移prefix动态包的加载/config/错误恢复/resize/DPI/输入隔离，记录实际图和库路径。真实独立用户会话测试由用户安排后执行；不要向终端/任意系统窗口注入输入。长期和多屏单列。

优先顺序：**GLM G27→G28→G29；Gemini随G28做V19、随G27做V20；G30逐项推进**。GLM无需等Codex解决所有求值余项才能开始其他任务。新部署通过后即可试装，不要求先完成整条路线图。
